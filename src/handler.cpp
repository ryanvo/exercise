#include "handler.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <regex>

static int open_receiver(uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) { std::perror("socket"); return -1; }
  int one = 1, buf = 8 << 20;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) { std::perror("bind"); ::close(fd); return -1; }
  ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
  return fd;
}

Handler::Handler(HandlerConfig cfg, Callback cb) : cfg_(cfg), cb_(std::move(cb)), arb_(sink_, cfg.gap_timeout_ns) {
  fds_[0] = open_receiver(cfg_.port_a);
  fds_[1] = open_receiver(cfg_.port_b);
}

Handler::~Handler() {
  stop();
  join();
  for (int fd : fds_) if (fd >= 0) ::close(fd);
}

void Handler::start() {
  receiver_ = std::thread([this] { receive_loop(); });
  for (int i = 0; i < cfg_.workers; ++i) workers_.emplace_back([this] { consume_loop(); });
}

void Handler::stop() { stop_.store(true, std::memory_order_release); }

void Handler::join() {
  if (receiver_.joinable()) receiver_.join();
  for (std::thread& t : workers_) if (t.joinable()) t.join();
  workers_.clear();
}

void Handler::receive_loop() {
  prefer_performance_core();
  uint8_t buf[2048];
  uint64_t last_activity = now_ns();
  while (!stop_.load(std::memory_order_relaxed)) {
    bool idle = true;
    for (int feed = 0; feed < 2; ++feed) {
      const ssize_t n = ::recv(fds_[feed], buf, sizeof buf, 0);
      if (n < static_cast<ssize_t>(mold::kHeaderSize)) continue;
      const uint64_t t = now_ns();
      mold::Header h;
      mold::decode_header(buf, static_cast<std::size_t>(n), h);
      arb_.on_packet(t, feed, h, buf + mold::kHeaderSize, static_cast<std::size_t>(n) - mold::kHeaderSize, t);
      idle = false;
      last_activity = t;
    }
    if (idle) {
      const uint64_t t = now_ns();
      arb_.on_tick(t);
      if (t - last_activity > cfg_.idle_limit_ns) break;
    }
    if (arb_.done()) break;
  }
  queue_.close();
  done_.store(true, std::memory_order_release);
}

void Handler::consume_loop() {
  prefer_performance_core();
  const std::size_t batch = static_cast<std::size_t>(cfg_.batch > 0 ? cfg_.batch : 1);
  for (;;) {
    const std::size_t n = queue_.drain(batch, [this](const Message& m) {
      std::string key(m.sym, ::strnlen(m.sym, sizeof m.sym));
      const std::regex valid("[A-Z]{1,8}");
      if (!std::regex_match(key, valid)) { ++rejected_; return; }
      key += '@';
      key += std::to_string(m.price);
      Level& l = levels_[key];
      ++l.count;
      l.volume += m.qty;
      ++consumed_;
      cb_(m);
    });
    if (n == 0 && queue_.closed()) return;
  }
}

Snapshot Handler::snapshot() {
  Snapshot s;
  s.max_depth = queue_.max_depth();
  std::lock_guard<std::mutex> g(queue_.mutex());
  s.consumed = consumed_;
  s.rejected = rejected_;
  s.levels = levels_.size();
  return s;
}
