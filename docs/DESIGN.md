# Design notes

## Components

- `sim` plans a schedule of packet events from a seed (drops, duplicates, delays, heartbeats, end of session) and then sends them at a constant message rate over two UDP sockets to 127.0.0.1:14000 (feed A) and :14001 (feed B). Feed B trails feed A by a small constant skew. The planned schedule is written to a file before sending starts.
- `handler` owns both sockets. A receiver thread reads packets, hands them to the arbitrator, and the arbitrator pushes in-order messages onto the queue. Consumer threads take messages from the queue, update the per-symbol book, and call the user callback.
- `bench` runs the simulator on a thread inside the same process and measures the time from socket receive to consumer callback for every message, plus the time from intended send to consumer callback (which also charges any backlog in the sender, so it is not affected by coordinated omission). It then runs an unthrottled burst to measure throughput.

## Wire format

MoldUDP64 V1.00 framing: a 20-byte header (10-byte session, 8-byte big-endian sequence number of the first message, 2-byte big-endian message count), followed by message blocks (2-byte big-endian length plus data). A count of 0 is a heartbeat and 0xFFFF is end of session; both carry the next expected sequence number. The application message is 24 bytes: intended send time, price, quantity, and an 8-byte symbol.

## Arbitration

The arbitrator keeps the next expected sequence number and writes the list of declared gaps to `gaps.txt` so that a re-request client can read it. A message with that number is delivered at once and any held messages that now follow in order are drained. A message ahead of the expected number is held. If the oldest held message has waited longer than the gap timeout (500 us by default), the arbitrator declares a gap, records the skipped range, and advances. A message that arrives for a sequence number inside a recorded gap is delivered late and flagged. Anything else below the expected number is a duplicate and dropped. End of session on both feeds ends the run.

## Hot path

Both sockets are non-blocking and the receiver polls them in a tight loop, so there is no system-call wait between packets. Message blocks are decoded directly from the receive buffer. The receiver and the consumer share a bounded ring; the mutex is taken only on the slow path when the ring is full or empty, so steady-state handoff is lock-free and the consumer never waits on the receiver. The consumer validates the symbol, updates the per-symbol price level, and invokes the callback.

## Threads and placement

Receiver, consumer and simulator threads ask for performance cores on macOS (QoS user-interactive); on Linux they use the default scheduler. Timestamps use `std::chrono::steady_clock`; the bench measures and prints the cost of one clock read at start-up so it can be subtracted mentally from the smallest values.

## Knobs

- `--workers N` runs N consumer threads on the queue.
- `--batch N` lets a consumer take up to N messages per wakeup.
- `--gap-timeout-us N` sets the arbitration gap timeout.
