# RemoteOps — Remote System Monitoring & Management Tool

**IE3090 Network Programming — Assignment Part 1 (Take-Home Implementation)**

**Student Registration Number:** IT24200382
**Module:** IE3090 Network Programming (Year 3, Semester 1)
**Submission Date:** 5th October 2026

---

## 1. Personalised Values (from Registration Number IT24200382)

| Item | Formula | Value |
|------|---------|-------|
| Agent listening port | 7000 + first four digits (`2420`) | **9420** |
| Session ID (SID) tag | Last four digits reversed (`0382` → `2830`) | **2830** |
| Authentication token | `OPS-` + last four digits | **OPS-0382** |
| Log file name | `remoteops_<regno>.log` | **remoteops_IT24200382.log** |
| Storage path | `./agentfiles/<regno>/<filename>` | **./agentfiles/IT24200382/** |
| Source file names | `agent_<last3digits>.c` etc. | **agent_382.c**, **controller_382.c**, **Makefile_382** |
| Submission archive | `IE3090_<regno>.zip` | **IE3090_IT24200382.zip** |

---

## 2. Overview

RemoteOps is a client/server system for remote system monitoring and management over TCP/IP, with a secondary UDP channel for periodic monitoring.

**Components:**
- **Agent** (`agent_382.c`) — the server, running on the managed machine. Listens on TCP port **9420**, serves multiple simultaneous Controllers, and provides system info, process listing, whitelisted command execution, file upload/download, and periodic UDP monitoring.
- **Controller** (`controller_382.c`) — the client, used by the administrator. Connects to the Agent on port 9420, authenticates with the token **OPS-0382**, and issues commands via a menu-driven interface.

---

## 3. Build Instructions

### Requirements
- Linux environment (tested on CentOS Stream 10)
- GCC compiler with pthread support
- GNU Make

### Build

```bash
make -f Makefile_382
