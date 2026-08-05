# EINTR unhandled in the event loop

*Fixed: 2026-08-05, branch `bug-hunt-2`. Covers the `EINTR` half of **issue 6** in `docs/Gaps_and_Issues.md` (`EpollLoop: use-after-del within a batch, and EINTR unhandled`) and the `- [ ] EINTR unhandled` entry in `docs/TODO.md`. The use-after-del half of issue 6 is **not** addressed here and remains open.*

---

## 1. What `EINTR` is

`EINTR` ("Interrupted system call") is what the kernel returns from a **blocking** syscall when a signal arrives while the process is asleep inside it. The call returns `-1` with `errno == EINTR`, having done nothing: no events reported, no data moved, nothing consumed. It is not an error — it means *"I aborted your wait to deal with a signal, call me again."*

Two properties made it a live problem for `EpollLoop::run()`:

1. **`epoll_wait` is in the never-restarted class.** Linux can transparently re-issue an interrupted syscall (`SA_RESTART`), and glibc's `signal()` — which `EpollLoop.cpp` uses to install the SIGINT handler — enables that by default. This is why `read`/`write`/`accept`/`waitpid` almost never surface `EINTR`. But `epoll_wait`, `select`, `poll` and `nanosleep` are explicitly excluded, because they carry a timeout the kernel cannot honestly restart. `epoll_wait` is the one call in the program that is *forced* to hand `EINTR` back to us.
2. **The process is asleep in that call almost always.** With a 5 ms timeout and an idle server, the loop body takes microseconds and the process is inside `epoll_wait` ~99% of wall-clock. Any signal lands there.

## 2. What was broken

```cpp
int ready = epoll_wait(_epoll_fd, _events, MAX_EVENTS, 5);
// TODO EINTR not handled
// if (ready < 0 && errno == EINTR) continue;      <-- commented out
if (ready < 0 && sig_int == false) {
    std::cerr << "epoll_wait() failed" << std::endl;
    break;                                          // leaves run() -> process exits
}
```

`errno` was never consulted, so **every** negative return was treated as fatal, and fatal meant `break` out of `run()` → return to `main()` → process exit. A benign retry-me condition terminated the server.

The `sig_int == false` guard only rescued Ctrl-C: SIGINT sets the flag, the guard is false, `ready == -1` makes the dispatch loop a no-op, and the `while` condition ends the loop cleanly. That path was correct. Every *other* signal was not.

### The reachable failure, with no code changes

| Step | Event       | Where the process is                                                             |
| ---- | ----------- | -------------------------------------------------------------------------------- |
| 1    | Server idle | asleep in `epoll_wait`                                                           |
| 2    | **Ctrl-Z**  | terminal sends `SIGTSTP`; kernel must stop the process, so it aborts the syscall |
| 3    | **`fg`**    | shell sends `SIGCONT`, process resumes after the aborted call                    |
| 4    | —           | `epoll_wait` returns `-1`, `errno = EINTR`                                       |
| 5    | —           | `sig_int` is still false → prints `epoll_wait() failed`, `break`                 |
| 6    | —           | **server exits**; every connection, upload and CGI child lost                    |

`strace -p <pid>` / attaching gdb (both use ptrace stops) does the same thing. Reproduced with a standalone `epoll_wait` loop: `SIGUSR1` with an ordinary `signal()`-installed handler, and two `SIGTSTP`/`SIGCONT` cycles, produced three `errno=4 (Interrupted system call)` returns — confirming `SA_RESTART` does not cover `epoll_wait`.

### Why it mattered

The subject requires the server to remain operational at all times; a server that dies during evaluation is grade 0. This was the cheapest possible way to fail that: not a subtle race under load, but `Ctrl-Z; fg` at a shell prompt, or a reviewer running `strace` to see what the loop does.

Secondary cost: because `errno` was never inspected, a genuine `EBADF` and a harmless `EINTR` produced the identical log line.

## 3. Fixes applied

### 3.1 `EpollLoop::run()` — the `epoll_wait` call

```cpp
int ready = epoll_wait(_epoll_fd, _events, MAX_EVENTS, 5);
int err = errno;                 // capture before anything can clobber it
if (ready < 0) {
    if (err != EINTR && sig_int == false) {
        std::cerr << "epoll_wait() failed: " << strerror(err) << std::endl;
        break ;
    }
    ready = 0;
}
```

- **`ready = 0` rather than `continue`.** `continue` would skip the timeout sweep, `clear()` and `reap_children()` for that iteration. Harmless at a 5 ms tick, but "the poll returned no events" is the honest model and keeps a single path through the loop body.
- **`errno` captured immediately.** `std::cerr <<` — or anything else — is allowed to overwrite it.
- **Ordering preserved.** A real error during shutdown (`sig_int` set) falls into `ready = 0` and the `while` condition ends the loop cleanly, as before.

### 3.2 `EpollLoop::reap_children()` — `waitpid(WNOHANG)`

Before, `-1` was misread as "reaped":

```cpp
// Non-zero means reaped (<0) or already gone (>0) - stop tracking either way
if (waitpid(it->first, NULL, WNOHANG) != 0) { _children.erase(it++); continue ; }
```

An `EINTR` return dropped a **live** child from `_children`, losing the SIGTERM/SIGKILL escalation and leaking a zombie. After:

```cpp
pid_t ret = waitpid(it->first, NULL, WNOHANG);
if (ret > 0 || (ret < 0 && errno == ECHILD)) {
    _children.erase(it++);
    continue ;
}
// ret == 0 (still running) or ret < 0 with EINTR (state unknown): assume still
// running and fall through to the deadline check. A signal sent to a zombie is
// discarded, so the conservative branch is safe; the next sweep re-checks.
```

The three `waitpid` answers and the required response:

| Return          | Meaning                                 | Action                    |
| --------------- | --------------------------------------- | ------------------------- |
| `> 0`           | reaped, status collected                | stop tracking             |
| `0`             | still running                           | keep, re-check next sweep |
| `-1` + `ECHILD` | **the kernel has no child by that pid** | stop tracking             |
| `-1` + `EINTR`  | interrupted, nothing learned            | keep, re-check next sweep |

**Why `ECHILD` must erase.** An `errno`-free variant (`if (ret > 0)` only) was considered and rejected: it leaves the entry in `_children` permanently, and the deadline branch then re-`kill()`s that pid every `CGI_KILL_GRACE` seconds forever. Pid numbers are recycled once the kernel wraps around `/proc/sys/kernel/pid_max` (4194304 here, but commonly 32768 in containers and lab machines), and `kill()` checks permission, not parentage — so a same-uid process that later inherits the number gets signalled. `ECHILD` means "this pid is not mine anymore," and the only safe response is to stop touching it.

Realistic routes to `ECHILD` in this codebase: adding `signal(SIGCHLD, SIG_IGN)` as a zombie fix (makes the kernel auto-reap, so *every* subsequent `waitpid` returns `ECHILD`); the destructor's own reap loop; or registering a pid that was never a child — see the uninitialised `_cgi_pid` in issue 16 of `Gaps_and_Issues.md`.

### 3.3 `EpollLoop::~EpollLoop()` — blocking `waitpid`

An interrupted blocking wait returned `-1` and left the child unreaped, so shutdown could exit with a zombie it had just SIGKILLed.

```cpp
kill(it->first, SIGKILL);
while (waitpid(it->first, NULL, 0) < 0 && errno == EINTR)
    ;   // retry: a signal during shutdown must not orphan the child
```

Cannot spin: any other error (`ECHILD`) exits immediately, and the child is already SIGKILLed so the wait resolves.

### 3.4 Signal-handler hygiene (same code path)

```cpp
static volatile sig_atomic_t    sig_int    = 0;

void    int_handler(int) {
    sig_int = 1;
}
```

Three changes, all in the handler this issue lives next to:

- **Nothing but the flag write.** The handler previously called `LOG_DEBUG`, i.e. `operator<<` on a `std::ostream` — not async-signal-safe. It takes a lock and allocates; if the signal lands while the main flow is mid-`<<` on the same stream, the handler waits on a lock only the interrupted code can release, and Ctrl-C hangs.

- **Unnamed `int` parameter.** `signal()` requires a `void (*)(int)`, so the parameter cannot be removed — but leaving it unnamed satisfies the signature with nothing for `-Wunused-parameter` to flag under `-Wall -Wextra -Werror`.

- **`volatile sig_atomic_t`, not `bool`.** `sig_atomic_t` guarantees the read/write is indivisible (no torn value if the signal lands mid-store); `volatile` stops the compiler caching the flag in a register across the loop. The C++ standard permits a handler to touch a static object *only* by assigning to a `volatile sig_atomic_t` — anything else is UB.
  
  Verified with codegen. At `-O3` a spin loop on a non-`volatile` static compiles to `.L2: jmp .L2` — the load is deleted outright and Ctrl-C can never be observed. Adding an opaque function call to the body does **not** save it (`call opaque; jmp .L9`), because the flag's address never escapes the translation unit. The real `run()` happens to survive `-O3` today only because `int_handler`'s address escapes via `signal()`, forcing a reload — accidental correctness the standard does not owe us, and the Makefile builds with `-O3`.

### 3.5 Missing `SIGKILL` (found while patching, not `EINTR`)

The escalation branch logged `ignored SIGTERM, sending SIGKILL` but never called `kill()` — a CGI child that ignores SIGTERM was never actually killed and looped through the warning forever. `kill(it->first, SIGKILL);` added. This is a "must not hang" defect adjacent to issue 9.

## 4. Deliberately not changed

- **`accept()` (`ServerConnection.cpp:21-22`).** `if (client_fd < 0) return;` is already `EINTR`-correct: epoll registration is level-triggered (`EPOLLIN | EPOLLERR | EPOLLHUP`, no `EPOLLET`), so the pending connection stays queued and the listen fd re-reports `EPOLLIN` 5 ms later. The `TODO` there is the **EMFILE** busy-spin of issue 10 — a different problem needing a different fix.
- **`read`/`write` on sockets (`ScratchBuffer.cpp:65`, `:89`).** `subject.txt:58-59` forbids checking `errno` after a read or write. Nothing is needed: both are in the restartable class and the handler carries `SA_RESTART`, so the kernel re-issues them; `SIGPIPE` is `SIG_IGN` (`webserv.cpp:23`) and ignored signals never interrupt. Where `EINTR` could still surface we are required to treat `-1` as "connection is done" regardless — which is what the code does.
- **`src/CgiConnection.cpp:10,23`.** Two unguarded `waitpid` calls, but the file is not in the Makefile's `SRCS` — dead code, tracked under issue 24.

## 5. Subject compliance

`subject.txt:58-59` reads:

> Checking the value of errno to adjust the server behaviour is strictly forbidden **after performing a read or write operation.**

The prohibition is scoped to read/write, and `errno` and `strerror` are both on the allowed-functions list (`subject.txt:20`). `epoll_wait` is the readiness call, not I/O; `waitpid` is neither. The rule exists to stop `errno` being used as a *substitute* for poll readiness (`read()` + `EAGAIN` busy-polling), which `subject.txt:63-66` spells out — reading `errno` after `epoll_wait` does the opposite, it is how the single readiness call keeps working.

The pre-existing `errno` uses in the tree are all in the same legal category: `epoll_create1` (`EpollLoop.cpp:36`), `fcntl`/`socket`/`bind`/`listen` (`utils.cpp:13-30`), `readdir` (`autoindex.cpp:23-27`), `stat` (`autoindex_File.cpp:21`). There is no `errno` check after any `read`/`write`/`recv`/`send` in the compiled sources.

## 6. Verifying

- `Ctrl-Z` then `fg` on a running `./webserv` is a no-op: clients stay connected, no `epoll_wait() failed`, process alive. Same for `strace -p <pid>` attaching and detaching.
- `grep -rn "waitpid\|epoll_wait" src/` shows no unguarded call site outside the dead `CgiConnection.cpp`.

## 7. Follow-ups left open

- 
- **Issue 6's other half:** use-after-del within an `epoll_wait` batch is still unfixed; the `TODO` at `EpollLoop.cpp:111-115` stands.
- **Shutdown log.** `int_handler` no longer logs. If the SIGINT message is wanted back, store the signal number (`sig_int = sig;`) and log after the loop exits in `run()`, where `std::ostream` is safe.
