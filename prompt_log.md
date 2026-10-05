# AI Prompt Log — RemoteOps (IT24200382)

**Student:** Hiruni
**Registration Number:** IT24200382
**Assignment:** IE3090 Network Programming — Assignment Part 1
**AI Tool Used:** ChatGPT (OpenAI)

---

## Declaration

I used ChatGPT as an AI assistant during Part 1 of this assignment, in line with the **CLEAR Level 3 (AI Collaboration)** rules in §3 of the assignment brief. All AI-generated code, architecture suggestions, and prose were **critically reviewed, tested, and modified** by me before inclusion in this submission. I understand every part of the final code and am prepared to explain or modify it in the Lab Assessment and Viva without AI assistance.

The entries below record the substantive AI interactions — prompts I sent, what the AI provided, and how I used or changed the output.

---

## Interaction 1 — Initial planning and protocol analysis

**Prompt:** "I have an assignment to build a RemoteOps client/server tool in C using BSD sockets. The protocol is fully specified. I need help understanding the exact framing rules — particularly how PUT and GET handle raw bytes after a text header."

**AI provided:**
- An explanation of TCP's stream semantics and why `recv()` does not preserve message boundaries.
- A suggested helper function `recv_all()` that loops until N bytes are read.
- A warning that `recv_line()` must read one byte at a time to find the `\n` terminator.

**How I used it:**
- I wrote my own `recv_line()` and `recv_all()` helpers based on this guidance.
- I tested both with a byte-counting `printf` to confirm they handled partial reads correctly.
- I chose to keep `recv_line` reading one byte at a time (simple, correct) rather than buffering (complex, premature optimisation).

---

## Interaction 2 — Concurrency model choice

**Prompt:** "The brief allows threads, fork, or select/poll/epoll. Which is best for a small server with ≤5 concurrent clients on Linux, and why?"

**AI provided:**
- A comparison of the three approaches with pros and cons.
- A recommendation of thread-per-connection for simplicity, given shared log file access and small client count.

**How I used it:**
- I agreed with the recommendation and implemented `pthread_create` + `pthread_detach` per accepted connection.
- I wrote the justification in my Implementation Report in my own words.

---

## Interaction 3 — Skeleton Agent code

**Prompt:** "Can you give me a skeleton for a TCP server in C that accepts connections in a loop and spawns a thread per client?"

**AI provided:**
- A minimal `socket()` → `bind()` → `listen()` → `accept()` loop.
- A `client_thread` function stub.

**How I used it:**
- I pasted the skeleton into `agent_382.c` and then heavily modified it:
  - Added `SO_REUSEADDR` to avoid "Address already in use" after restarts.
  - Added a `session_t` struct to carry per-connection state (socket, peer IP, auth flag, monitor info).
  - Added signal handling for `SIGPIPE` so the server doesn't die when a client disconnects mid-write.
- The skeleton was ~30 lines; my final `agent_382.c` is ~505 lines.

---

## Interaction 4 — SYSINFO implementation

**Prompt:** "How do I read CPU load, memory usage, and uptime from /proc on Linux in C?"

**AI provided:**
- Pointers to `/proc/loadavg`, `/proc/meminfo`, `/proc/uptime` and example `fscanf` code for each.

**How I used it:**
- I wrote my own `get_cpu_load()`, `get_mem_used_mb()`, and `get_uptime_sec()` functions based on this guidance.
- For memory, I chose to compute **used = MemTotal - MemAvailable** (rather than MemFree) because MemAvailable is a better indicator of actually-usable memory. This was my own decision.

---

## Interaction 5 — PUT/GET byte-counting bug

**Prompt:** "My PUT command is hanging when the Controller sends a file. I read the header line, then try to read the file bytes with recv_line, but it blocks. What's wrong?"

**AI provided:**
- Explained that the file payload has no `\n` terminator, so `recv_line` blocks forever.
- Suggested switching to a `recv_all` loop for the payload.

**How I used it:**
- I switched my `cmd_put` handler to use `recv_all()` in a loop, decrementing the remaining byte count.
- I added a matching `send_all()` loop on the Controller side.
- I verified with MD5: uploaded a 60-byte file, downloaded it back, and confirmed identical hashes (`acd417486326c1f15a183e53ad28209f`).

---

## Interaction 6 — UDP monitoring thread

**Prompt:** "I want the Agent to send periodic SYSINFO datagrams to the Controller's UDP port when the Controller sends MONITOR START <port>. How should I structure this?"

**AI provided:**
- Suggested storing the client's UDP port and IP in the session, then spawning a detached thread that loops `sendto()` + `sleep()`.

**How I used it:**
- I implemented it exactly as described.
- **Bug I found myself:** the monitor thread didn't reliably see `monitor_active = 0` when the main thread set it. I fixed this by declaring `monitor_active` as **`volatile int`**. The AI did not mention this — I found it while debugging.
- I also made sure to set `monitor_active = 0` at the end of `client_thread()` so the monitor stops when the Controller disconnects.

---

## Interaction 7 — Log file thread safety

**Prompt:** "My log file has interleaved lines when two clients connect at the same time. How do I fix it?"

**AI provided:**
- Suggested a `pthread_mutex_t` around the log writes.

**How I used it:**
- Added `pthread_mutex_lock/unlock` around the `fprintf` in my `log_event()` function.
- Also added `fflush(g_log)` after every write so I could `tail -f` the log during live testing.
- The fix worked: the log file is clean even with multiple concurrent clients.

---

## Interaction 8 — Implementation Report structure

**Prompt:** "What should I include in the Implementation Report for this assignment?"

**AI provided:**
- A suggested outline based on §2.7 of the brief.

**How I used it:**
- I wrote the report myself, using the outline as a checklist.
- All prose, screenshots, and testing evidence are my own.
- All screenshots are genuine captures of my program running on my CentOS VM.

---

## Summary of AI Usage

| Stage | AI used? | What AI did | What I did myself |
|-------|----------|-------------|-------------------|
| Planning & architecture | Yes | Explained framing rules, compared concurrency models | Chose threads, wrote design diary |
| Skeleton code | Yes | ~30-line TCP server skeleton | Expanded to 505 lines, added session struct, signal handling |
| Command handlers | Yes (SYSINFO hint) | Pointed to /proc files | Wrote all handlers myself, chose MemAvailable |
| PUT/GET | Yes (debugging) | Explained recv_line vs recv_all | Wrote both functions, verified with MD5 |
| UDP monitor | Yes (structuring) | Suggested thread design | Found and fixed `volatile` bug myself |
| Logging | Yes (mutex) | Suggested mutex | Wrote log_event function, added fflush |
| Report | Partially | Outline suggestion | Wrote all content myself |

**I confirm:** Every line of code in this submission was read, understood, and tested by me. I did not paste AI output blindly. The Lab Assessment and Viva will be completed without AI, and I am prepared to modify and explain this code live.
