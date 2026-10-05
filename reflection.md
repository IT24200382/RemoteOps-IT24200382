# Structured Reflection — RemoteOps (IT24200382)

**Student:** Hiruni
**Registration Number:** IT24200382
**Module:** IE3090 Network Programming
**Assignment:** Part 1 — Take-Home Implementation

---

## 1. Which AI tools did I use, and at which stages of the work?

I used **ChatGPT (OpenAI)** as an AI assistant across the whole build, but most heavily in three stages:

- **Early planning** — to understand TCP framing rules and compare concurrency models (threads vs fork vs select/poll).
- **Debugging** — when my PUT command hung because I tried to read binary file data with a line-based reader.
- **Conceptual gaps** — for example, when I needed a hint on how to structure the periodic UDP monitor thread.

I did **not** use AI for the actual writing of the log file, the design diary, this reflection, or the final report — those are my own words describing my own work.

---

## 2. What did the AI do well?

The AI was genuinely helpful in three ways:

1. **Explaining concepts clearly.** TCP's stream semantics — specifically that `recv()` has no concept of message boundaries — was something I'd read about but hadn't really internalised. The AI's explanation of *why* `recv_line()` has to read one byte at a time, and *why* PUT/GET payloads need a different function, finally made it click.

2. **Providing good starting skeletons.** A ~30-line TCP server skeleton saved me time on boilerplate that I would have needed to look up anyway. I still had to expand it a lot, but it removed the initial friction.

3. **Catching my bugs.** When my PUT handler hung, the AI immediately identified the cause (using `recv_line` on binary data). This was a real time-saver.

---

## 3. Where did the AI get things wrong or mislead me?

The most important example: **the AI did not warn me about the `volatile` keyword** for the `monitor_active` flag shared between the main connection thread and the UDP monitor thread. My monitor thread sometimes didn't stop when it should have, because the compiler was caching the flag in a register. I found this by adding debug `printf` statements and noticing the loop wasn't seeing updates. Declaring `monitor_active` as `volatile int` fixed it. The lesson: an AI answer that "looks complete" can still miss subtle concurrency issues that only show up when you actually run the code.

The AI also initially suggested using `send()` once and assuming it would send the whole buffer. That's wrong on real networks — `send()` can return fewer bytes than requested. I had to write `send_all()` and `recv_all()` helpers to loop until the full buffer is transferred. I discovered this by reading the man pages carefully rather than trusting the AI's simplified example.

---

## 4. What did I change, add, or reject from any AI output, and why?

Changes and additions I made on top of AI suggestions:

- **Added `SO_REUSEADDR`** to the socket setup — not in the AI's skeleton. This was necessary because otherwise, restarting the Agent after an unclean shutdown gave "Address already in use".
- **Rejected the AI's `send()` example** and wrote `send_all()` / `recv_all()` loops.
- **Added `SIGPIPE` handling** — the AI did not mention that a `send()` to a disconnected socket would kill the process by default.
- **Chose `MemAvailable`** instead of `MemFree` for the memory calculation — the AI gave both, and I made the design decision based on which is more meaningful on Linux.
- **Wrote the log function myself** with a mutex and `fflush()` — the AI only suggested the mutex, not the flush, which I needed to make the log usable during live testing.

---

## 5. What did I learn about my own understanding of network programming?

Two lessons stand out.

First, **framing is the whole game**. Before this assignment, I thought of a "message" as something the network delivers intact. It doesn't. TCP is a byte stream, and every protocol that runs on top of it has to define its own framing. This assignment forced me to think about framing in three places at once: lines (`\n`), fixed-size binary payloads (PUT/GET byte counts), and datagrams (UDP preserves boundaries, which was a nice contrast).

Second, **concurrency is subtle**. Threads sharing memory feels simple until you forget about `volatile`, or forget to mutex-protect a shared file, or forget to signal a monitor thread to stop. Every one of these bugs actually appeared in my code, and I only found them by testing and reading my own log file. The AI was a useful sounding board, but the responsibility to actually understand what the code does — and to prove it works — was mine alone.

I feel significantly more confident now writing socket code from scratch and debugging it methodically.
