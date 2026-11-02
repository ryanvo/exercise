#pragma once
#include <chrono>
#include <cstdint>
#include <cstring>
#ifdef __APPLE__
#include <pthread.h>
#include <sys/qos.h>
#endif

// Application payload carried in a MoldUDP64 message block (24 bytes, host byte order).
struct WireMsg {
  uint64_t send_ns;  // intended send time of the packet (steady_clock)
  uint32_t price;
  uint32_t qty;
  char sym[8];
};
static_assert(sizeof(WireMsg) == 24, "WireMsg layout");

// In-process representation handed from the receiver to the consumer.
struct Message {
  uint64_t seq;
  uint64_t send_ns;
  uint64_t recv_ns;  // socket receive timestamp
  uint32_t price;
  uint32_t qty;
  char sym[8];
  uint8_t feed;
  bool late;
};

constexpr int kSymbolCount = 50;
inline const char* const kSymbols[kSymbolCount] = {
    "AAPL", "MSFT", "AMZN", "GOOG", "META", "NVDA", "TSLA", "BRKB", "JPM",  "V",
    "UNH",  "XOM",  "JNJ",  "WMT",  "PG",   "MA",   "HD",   "CVX",  "LLY",  "ABBV",
    "MRK",  "PEP",  "KO",   "AVGO", "COST", "TMO",  "MCD",  "CSCO", "ACN",  "ABT",
    "DHR",  "NKE",  "LIN",  "ADBE", "TXN",  "NEE",  "CRM",  "PM",   "BMY",  "RTX",
    "AMGN", "HON",  "UPS",  "QCOM", "LOW",  "INTC", "IBM",  "CAT",  "BA",   "GS"};

inline uint64_t now_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Ask the OS to keep this thread on a performance core (macOS); no-op elsewhere.
inline void prefer_performance_core() {
#ifdef __APPLE__
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}
