# feed-exercise

A small A/B market data feed handler: a simulator that publishes one sequenced message stream over two UDP feeds, a handler that merges the two feeds into one in-order stream for a consumer thread, and a bench that measures the handler.

## Glossary

A **feed** is one UDP stream of MoldUDP64 packets that carries the exchange's messages; this repo has feed A and feed B on two localhost ports. Every message carries a **sequence number**, assigned once by the publisher, so that receivers can tell where each message belongs and whether something is missing. A **gap** is a run of sequence numbers the handler has not received from either feed after waiting long enough; the handler records it and moves on, and a message from inside a gap that turns up later is delivered late. **A/B arbitration** means taking the first copy of each sequence number from whichever feed delivers it first, and discarding the second copy. Exchanges publish two feeds because any one network path loses or delays packets, and two independent paths make the stream complete and timely far more often than one.

## Build and run

```
cmake --preset release && cmake --build --preset release      # also: asan, tsan
./build/release/handler &                                     # listens on 127.0.0.1:14000 (A) and :14001 (B)
./build/release/sim                                           # publishes 200k messages at 50k/s, writes schedule.txt
./build/release/bench --runs 3                                # in-process sim + handler, p50/p99/p99.9 and throughput
```

On Linux, raise the socket buffer limit once before running anything: `sudo sysctl -w net.core.rmem_max=8388608` (the handler asks for an 8 MiB receive buffer; the default cap is about 200 KiB and the kernel will drop packets).

`scripts/run.sh [sim args]` starts the handler and the sim together. Options for `sim`, `handler` and `bench` are listed at the top of their `main` functions; the schedule file lists every packet event (send, drop, dup, delay, heartbeat, end) with its planned time, so a run can be reproduced and checked packet by packet. `bench --runs N` prints per-run results and the spread across runs.

## The task

1. **Correctness.** Under the default simulator settings the handler does not deliver the stream correctly. Find the cause, fix it, and show with the schedule file that the output is now right.
2. **Tail.** The recv->consumer p99.9 that `bench` reports is unacceptable for this handler. Find the causes, fix them, and prove each fix with the bench.
3. **Design.** Be ready to discuss how you would take this handler to production.

Claude Code is expected for this exercise, and the session transcript will be reviewed together with the code.

At the end, export the transcript with: `cp "$(ls -t ~/.claude/projects/*feed-exercise*/*.jsonl | head -1)" ./transcript.jsonl`
