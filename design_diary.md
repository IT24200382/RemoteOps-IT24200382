# Design Diary — RemoteOps (IT24200382)

**Project:** RemoteOps — Remote System Monitoring and Management Tool
**Registration Number:** IT24200382
**Period:** 29 September – 5 October 2026

---

## Entry 1 — 29 September 2026 — Planning the architecture

Read the assignment brief carefully. The biggest early decision was the **concurrency model** for the Agent. The brief allows threads, forked processes, or I/O multiplexing (select/poll/epoll). I chose **thread-per-connection** because:

- Threads share the process address space, making it trivial to share the log file handle and the global storage directory path.
- The workload is I/O-bound (waiting on `recv`, `send`, `popen`), so CPU contention is not a concern.
- Linux `pthreads` are well documented and easier to reason about than `epoll` state machines for a small number of clients (spec says ≥5 concurrent).

I sketched the protocol handler as a state machine: on each connection, the first command must be `AUTH`; until then, every other command is rejected with `ERR 001 AUTH_FAILED`.

---

## Entry 2 — 30 September 2026 — Personalisation and framing

Worked out my personalised values from IT24200382:

- Port: 7000 + first four digits (`2420`) = **9420**
- SID: last four digits (`0382`) reversed = **2830**
- Token: `OPS-` + last four digits = **OPS-0382**
- Log: `remoteops_IT24200382.log`
- Storage: `./agentfiles/IT24200382/`

Wrote helper functions `send_all()` and `recv_all()` because I realised early that **`send()` and `recv()` can return partial counts**. This was critical for `PUT`/`GET` where the file size is fixed. I decided to always loop until either the full buffer is sent/received or an error occurs.

**Obstacle:** `recv()` does not preserve message boundaries. I had to write `recv_line()` that reads **one byte at a time until `\n`**, because the protocol is line-based. This is slow but correct and simple. For binary payloads, I switch to `recv_all()`.

---

## Entry 3 — 1 October 2026 — Command handlers

Implemented handlers one by one:

- `AUTH` — compared token with `strcmp`, set `session->authenticated = 1`.
- `SYSINFO` — parsed `/proc/loadavg`, `/proc/meminfo`, `/proc/uptime`.
- `LISTPROC` — used `popen("ps -e -o pid=,comm=", "r")` and joined the lines with commas.
- `EXEC` — whitelist check via `is_whitelisted()`, then `popen()` for the actual command.

**Obstacle:** `EXEC DISKFREE` outputs multi-line `df -h` output which breaks the "one line per response" rule. **Fix:** after reading the `popen` output into a buffer, I replace every `\n` with a single space. This keeps the response on one line so the Controller's `recv_line()` still works.

---

## Entry 4 — 2 October 2026 — File transfer (PUT/GET)

This was the hardest part. The protocol says: after `PUT <name> <size>\n`, the Controller immediately sends **exactly `<size>` raw bytes**, with no delimiter.

**Obstacle:** I initially tried to use `recv_line()` for the payload, which hung because there is no `\n` after the binary data. **Fix:** after parsing the command line, switch to `recv_all(sock, buf, chunk)` in a loop, decrementing `remain` by the number of bytes actually read.

Same for `GET` on the Agent side — I send the response line, then stream the file with `send_all()` in chunks.

**Verification:** I uploaded a 60-byte test file and downloaded it back. Ran `md5sum` on both — **hashes matched exactly** (`acd417486326c1f15a183e53ad28209f`). That confirmed byte-for-byte integrity.

---

## Entry 5 — 3 October 2026 — UDP monitoring

Added the UDP monitoring feature. When `MONITOR START <udp_port>` is received:

- Store the client's source IP and the requested UDP port in the session.
- Set `monitor_active = 1` and `pthread_create()` a monitor thread.
- The thread loops: `snprintf` a SYSINFO line, `sendto()` it via a UDP socket, then `sleep(2)`.

**Obstacle:** I forgot to make `monitor_active` volatile. The monitor thread sometimes didn't see updates from the main thread. **Fix:** declared it as `volatile int monitor_active;` in the session struct. On CentOS with GCC 14, the compiler needed this hint.

**Also fixed:** when the Controller disconnects (even ungracefully), the monitor thread must stop. I set `monitor_active = 0` at the end of `client_thread()` so the monitor thread exits its loop within 2 seconds.

---

## Entry 6 — 4 October 2026 — Logging and error handling

Made the log file thread-safe with a `pthread_mutex_t`. Every log line is timestamped with `strftime("%Y-%m-%d %H:%M:%S")`.

Log events:
- Agent startup
- Connection opened/closed
- Command received
- AUTH success/failure
- File transfer completion with byte count
- MONITOR start/stop
- Ungraceful disconnects (`recv error/EOF`)

**Obstacle:** Two threads writing to the log simultaneously produced interleaved lines. **Fix:** wrap every `fprintf` to the log with the mutex, and `fflush(g_log)` after each write so I could `tail -f` the file during testing.

---

## Entry 7 — 5 October 2026 — Testing and screenshots

Ran the full test suite with screenshots:

- Agent startup on port 9420
- AUTH success and AUTH failure
- SYSINFO, LISTPROC, EXEC × 5 whitelisted commands
- EXEC rejection: `rm`, `cat /etc/passwd`, `ls` all rejected with `ERR 002 COMMAND_NOT_ALLOWED`
- PUT upload — 60 bytes stored in `agentfiles/IT24200382/`
- GET download — MD5 match confirmed
- GET missing file — `ERR 005 FILE_NOT_FOUND`
- UDP MONITOR START — 5+ datagrams received
- UDP MONITOR STOP — clean stop
- Ungraceful disconnect (Ctrl+C) — Agent survived, accepted new connections
- Port proof with `ss -tlnp | grep 9420` — shows `agent_382` listening
- Full log with timestamps

**Final obstacle:** The `bind: Address already in use` error when restarting the Agent. **Fix:** kill the old agent process (`pkill -f agent_382`) before restarting. This is expected behaviour when the previous bind was not cleanly released.

---

## Summary of key design decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Concurrency model | Threads | Simpler than fork; shares log file; adequate for ≤5 clients |
| Line framing | Byte-by-byte `recv_line` | Protocol is line-based; simple and correct |
| Binary framing | `recv_all` / `send_all` loops | Handles partial sends/receives |
| Logging | Mutex-protected append file | Thread-safe; survived multi-client tests |
| UDP monitor | Dedicated detached thread per session | Stops cleanly on `MONITOR STOP` or disconnect |
| EXEC safety | Whitelist of 5 commands | Required by brief; blocks shell injection |
