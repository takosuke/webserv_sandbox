# Blocking `waitpid` in the event loop, and the CGI child registry that replaced it

*Fixed: 2026-08-05, branch `bug-hunt-2` (commits `6f4e2fa` "Set up cgi pid table reaping for blocking waitpid", `c182976` "Added tests for cgi pid reaping issues"). Closes **issue 5** in `docs/Gaps_and_Issues.md` (`finalize_cgi() blocks the event loop in waitpid`) and **issue 9** (`Zombie CGI processes on client abort`), and supplies the CGI runtime timeout listed under missing feature 3. Prerequisite **issue 16** (uninitialised `_cgi_pid`) fixed in the same pass. Related and documented separately: `EINTR_unhandled.md` (§3.2, §3.3 and §3.5 cover the `waitpid` error handling and the missing `SIGKILL` inside the sweep written here) and `FD_CLOEXEC.md` (§9 on how the two interact).*

---

## 1. What was broken

Two call sites, both marked `FIXME`, both `waitpid(pid, NULL, 0)` — a **blocking** wait with no `WNOHANG`:

| Site | Context |
|---|---|
| `finalize_cgi()` | the normal completion path, after the CGI's output had been staged to `/tmp/cgi_*` |
| `handle_cgi_output()`, 502 branch | the "header block never arrived" path |

The whole server is one thread running one `epoll_wait` loop. `waitpid(pid, NULL, 0)` sleeps in the kernel until *that specific child* changes state, and while the process sits there:

- `epoll_wait` is not called — no other connection makes progress, no new connection is accepted;
- the timeout sweep (`EpollLoop.cpp:126-139`) does not run, so the 408 machinery is frozen too;
- `sig_int` is only tested at the top of the loop, so Ctrl-C is unresponsive for the duration.

### 1.1 The wait was pointless

`finalize_cgi` passed `NULL` for the status. **Nothing in the response depended on the child's exit status** — the body was already on disk and reopened for reading before the wait. The call existed purely to avoid a zombie: bookkeeping, sitting on the critical path of every other client.

### 1.2 The 502 site was worse

Its guard is `readret == 0 || _buf.fill_capacity() <= 1`. The second half means *the header block did not fit in the scratch buffer* — the CGI can be perfectly healthy and still running. A blocking wait on a live child then has no bound at all. With a CGI that emits an endless header stream: the server stops draining the pipe, the pipe fills at 64 KB, the CGI blocks in `write()`, the server blocks in `waitpid()`. Permanent mutual deadlock, and no timeout can break it because the sweep needs the loop.

### 1.3 Reproduction

`www/cgi-bin/slow_exit.py` writes a complete CGI response, flushes, closes stdout, then sleeps 3 s. The close gives the server `readret == 0`, so it reaches `finalize_cgi()` with the child still alive. `cgi_robustness_test/test-slow-exiting-cgi-does-not-block-the-loop` fires that request, waits 400 ms, then asks for any other response within 1500 ms — and the server was asleep for the remaining ~2.6 s.

### 1.4 Why it was grade-0 class

`subject.txt` requires the server to *"remain non-blocking at all times"* and that *"a request to your server should never hang indefinitely."* An infinite-loop or slow-exit CGI is a standard evaluation probe. Arguing that `waitpid` is not a `read` or `write` will not survive a demo where one CGI visibly freezes the server.

### 1.5 The prerequisite defect

`_cgi_pid` was missing from the constructor's init list (issue 16), so both sites could wait on garbage. The failure modes are unpleasant and silent: a value that happens to be `-1` blocks until **any** child exits; `0` waits on the entire process group; positive garbage returns `ECHILD` harmlessly. Fixed to `_cgi_pid(-1)` at `ClientConnection.cpp:111`, and every use is now guarded.

## 2. Options considered

| | Approach | Rejected because |
|---|---|---|
| A | Central `while (waitpid(-1, NULL, WNOHANG) > 0);` in the loop | Two lines and correct as far as it goes, but reaps only children that *already exited* — it cannot bound a CGI that runs forever. |
| B | `kill(pid, SIGKILL)` then `waitpid` in place | The wait becomes bounded (microseconds), but it is still literally a blocking call in the loop, and at the 502 site it would kill a healthy CGI whose headers merely overflowed. |
| C | `signal(SIGCHLD, SIG_IGN)` | One line, kernel auto-reaps. But the disposition is **inherited across `execve`**, so a CGI that waits on its own subprocesses breaks; and every later `waitpid` returns `ECHILD`. |
| **D** | **Per-connection pid tracking with deadlines in the loop** | **Chosen.** Subsumes the CGI runtime timeout and the client-abort zombie fix instead of needing separate work for each. |

D was chosen over A specifically because A would have had to be redone: a runtime timeout is a separate subject gap, and the pid table is the natural place for it.

## 3. The design

`EpollLoop` owns a registry of live CGI children. **Nothing ever waits.** The loop `WNOHANG`s each tracked pid once per tick and escalates on any child past its deadline. The connection stops caring about its child the moment it has the output.

The invariant that makes it safe: **a tracked pid is always valid.** Only the registry reaps, and an unreaped child stays a zombie holding its pid — so the number cannot be recycled underneath us. (This is why the `ECHILD` handling in `reap_children` matters; see `EINTR_unhandled.md` §3.2.)

```cpp
// inc/EpollLoop.hpp:28-32, :48
struct Child {
    time_t  deadline;
    bool    signalled;      // has SIGTERM already been sent?
    Child() : deadline(0), signalled(false) { }
};
std::map<pid_t, Child>  _children;
```

`signalled` records which rung of the escalation ladder we are on. It is **not** what prevents signal spam — that is the `deadline = now + CGI_KILL_GRACE` reset at the bottom of the branch, which pushes the next attempt out. The two do different jobs: the deadline says *when* to act again, `signalled` says *what* to send.

### 3.1 The three entry points

| Function | Site | Role |
|---|---|---|
| `track_child(pid, timeout)` | `EpollLoop.cpp:156` | register a fresh child with `deadline = now + timeout` |
| `kill_child(pid)` | `EpollLoop.cpp:165` | back-date the deadline to `0` — "due now" |
| `reap_children()` | `EpollLoop.cpp:172` | the per-tick sweep; the only place that reaps or signals |

`kill_child` sets `deadline = 0` rather than carrying its own signalling logic, so there is exactly **one** place in the program that sends a signal to a CGI. `time_t` `0` is the epoch, so `now >= deadline` is unconditionally true and the next sweep treats the child as if it had overrun.

It deliberately does **not** reset `signalled`. Escalation must be monotonic: a child that already ignored SIGTERM should get SIGKILL next, not another SIGTERM restarting the ladder from the bottom.

### 3.2 Wiring into `ClientConnection`

| Site | Change |
|---|---|
| `setup_cgi()` `:871` | `track_child(pid, CGI_TIMEOUT)` right after `_cgi_pid = pid` |
| `finalize_cgi()` `:1097` | blocking `waitpid` deleted, replaced by `_cgi_pid = -1` — detach and answer |
| 502 branch `:932-933` | `kill_child(_cgi_pid); _cgi_pid = -1;` — this child may be alive and stuck, so ask for it to go |
| `~ClientConnection` `:122-123` | `if (_cgi_pid > 0) kill_child(_cgi_pid);` — closes issue 9 |

The `_cgi_pid = -1` in the middle two is what stops the destructor re-killing a child that already finished.

### 3.3 Two properties that fall out

**`slow_exit.py` needs no kill at all.** It closes stdout, so `finalize_cgi` runs, detaches, and responds immediately. The child then sleeps 3 s and exits well inside its 10 s deadline, and a later tick reaps it silently. Nothing ever blocks.

**Killing the child unwedges the connection for free.** When the registry kills an overrunning CGI, its stdout closes, the pipe goes EOF, and `handle_cgi_output` sees `readret == 0` — which drives the *existing* state machine to either the 502 branch or `finalize_cgi`. No connection-side timeout logic was needed to un-stick it.

That second point is the reason the process deadline is not optional: **the per-connection timeout cannot bound a CGI.** `update_timestamp()` fires on every `readret > 0` from the pipe (`ClientConnection.cpp:892-893`), so a CGI that chatters forever refreshes `_last_update` indefinitely.

### 3.4 Shutdown

`~EpollLoop` (`EpollLoop.cpp:40-45`) SIGKILLs and blocking-waits every remaining child. Blocking is fine here — it is the one place where there is no loop left to starve.

## 4. Constants

| Constant | Value | Where |
|---|---|---|
| `CGI_TIMEOUT` | 10 s | `inc/ClientConnection.hpp:15-17` |
| `CGI_KILL_GRACE` | 2 s | `inc/EpollLoop.hpp:11` |

Both are compile-time `#define`s — see §6.2.

## 5. Verification

### 5.1 Tests added

`webserv-tests/test/webserv_tests/cgi_lifecycle_test.clj` — six tests that assert on the **process table and `/proc`** rather than on socket bytes, because a server can answer every request correctly while leaking a child per request. Fixtures: `www/cgi-bin/hang_forever.py` (never writes, never exits) and `www/cgi-bin/sigterm_immune.py` (also ignores SIGTERM).

| Test | Pins |
|---|---|
| `test-normal-cgi-is-answered-and-reaped` | control — the reaper stays out of the way |
| `test-runaway-cgi-is-terminated-at-its-deadline` | a CGI that never writes or exits is bounded |
| `test-server-stays-responsive-while-a-cgi-runs-away` | the original issue-5 stall |
| `test-sigterm-immune-cgi-is-escalated-to-sigkill` | the escalation is *performed*, not merely logged |
| `test-aborted-cgi-requests-leave-no-zombies` | issue 9 — the destructor hand-off |
| `test-aborted-cgi-requests-do-not-leak-fds` | sockets and both pipe ends released |

New harness helpers in `server.clj`: `server-pid`, `child-pids`, `zombie-pids`, `open-fd-count`, `wait-until`, `abort-request`, `kill-stray-children!`. Note `read-proc` rather than `slurp` — `slurp` sizes its buffer from `InputStream.available()`, which procfs answers with `EINVAL`.

`cgi-robustness-test/test-slow-exiting-cgi-does-not-block-the-loop` now passes. Suite went from 96/127/16 to 102/143/14 — two distinct failures cleared, no regressions.

### 5.2 Mutation-checked

A test that cannot fail is worthless, so each bug was reintroduced and the suite re-run:

| Mutation | Result |
|---|---|
| `kill(it->first, SIGKILL)` removed from the escalation branch | exactly `test-sigterm-immune-cgi-is-escalated-to-sigkill` fails (2 assertions), nothing else |
| `reap_children()` call removed from `run()` | 8 assertions across 5 tests fail |

The first is the important one: it is the exact defect that shipped in the first draft of `reap_children` (the branch logged `sending SIGKILL` without calling `kill`), and it is invisible in the log — which claims success either way. See `EINTR_unhandled.md` §3.5.

### 5.3 Live probes

Runaway CGI on a fresh server: `status=502, elapsed=10s`, no children left, server responsive throughout.

SIGTERM-immune CGI: `SIGTERM logged: 1, SIGKILL logged: 1`, answered after 12 s (= `CGI_TIMEOUT + CGI_KILL_GRACE`), child gone, no zombies.

Abort storm — 40 aborted CGI requests, 10 of them SIGTERM-immune:

```
before: 6 fds
after:  6 fds, 0 children, 0 zombies, 0 epoll failures, still serving
SIGTERM/SIGKILL sent: 41 / 11
```

## 6. Still open in this area

### 6.1 No `504 Gateway Timeout`

An overrunning CGI currently produces a 502 (via the header-incomplete branch) or a truncated 200, not the accurate `504`. The optional layer of design D was not implemented: give `Child` an `owner` back-pointer, have `finalize_cgi` and `~ClientConnection` detach by setting `owner = NULL`, and on first deadline expiry call a `ClientConnection::on_cgi_timeout()` that builds the 504 and rearms to `EPOLLOUT`.

Deferred because of the hazard: **the destructor must detach**, or the registry holds a dangling pointer into a freed connection. Worth doing as its own step, on top of a green suite. Relevant to *"your HTTP response status codes must be accurate."*

### 6.2 `CGI_TIMEOUT` is not configurable

A compile-time `#define`, so it cannot be set per location or per server. Two costs: `cgi_lifecycle_test` takes ~60 s because every kill assertion must wait out the real 10 s deadline; and an evaluator cannot tune it. A `cgi_timeout` directive following the existing `client_header_timeout` pattern would fix both — and would let the tests run against a 1 s deadline. Note that `config::header` is **not** inherited Server→Location (issue 8), so a new directive must not repeat that mistake.

### 6.3 `~EpollLoop` ordering

The child kill/wait loop runs *before* the connection-deletion loop. A `~ClientConnection` firing during that second loop calls `kill_child`, which back-dates a deadline for a sweep that will never run again. Harmless today — those children were already SIGKILLed and reaped by the first loop — but moving the child loop after the connection loop would make the ordering say what it means.

### 6.4 Issue 17 — fd bookkeeping around CGI teardown

Untouched by this work and still listed in `Gaps_and_Issues.md`: a connection dying while `fd` is a CGI pipe never closes `_client_fd` (socket leak), and `EpollLoop::delete_conn` (which closes `conn->fd`) plus `~ClientConnection` (which closes `_cgi_stdout_fd`) can double-close the same descriptor. A double-close is nastier than it looks — it can close a descriptor number the process has since reused, e.g. the epoll fd, producing `EBADF` from `epoll_wait` far from the cause.

`test-aborted-cgi-requests-do-not-leak-fds` does not reach this path: in the abort case the connection is torn down *after* the kill has driven it back to `_client_fd`, so `conn->fd` is the socket by then. The double-close needs deletion while `fd` is still the pipe. Not currently covered by any test.

### 6.5 Issue 18 — oversized CGI header still 502s

`cgi-robustness-test/test-oversized-cgi-header-is-forwarded` remains red. This is the branch whose blocking `waitpid` was the worse of the two (§1.2); the wait is gone, but the *policy* — 502 rather than forwarding a header larger than the scratch buffer — is still undecided. Forwarding needs growable header buffering. Keeping the 502 is defensible; if that is the decision, retire the test rather than leave it red.

### 6.6 No test for the 502-branch `kill_child`

`test-runaway-cgi-is-terminated-at-its-deadline` exercises the deadline path, and the abort tests exercise the destructor path, but nothing specifically drives a CGI into the header-incomplete branch *while it is still alive* and then checks the child is killed. Would need a script that writes a partial header block and then hangs.

## 7. Subject compliance

`waitpid`, `kill` and `signal` are all on the allowed-functions list (`subject.txt:26-27`). `WNOHANG` is a flag to an allowed call.

`errno` is read after `waitpid` in `reap_children` (to distinguish `ECHILD`). `subject.txt:58-59` scopes its prohibition to *"after performing a read or write operation"*; `waitpid` is neither, and `errno` is itself on the allowed list. Reasoning in full: `EINTR_unhandled.md` §5.
