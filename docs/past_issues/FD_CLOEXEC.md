# Forked CGI children inherit every open descriptor

*Partially fixed: 2026-08-05, branch `bug-hunt-2`. Found while writing `cgi_lifecycle_test.clj` for the deferred-reaper work (issue 5 / 9 in `docs/Gaps_and_Issues.md`), where it broke the test suite outright. Three of the four descriptor classes are now marked close-on-exec; **the accepted client socket is not — see §6.** Not previously listed in `Gaps_and_Issues.md`.*

---

## 1. The mechanism

`fork()` duplicates the entire file-descriptor table, and `execve()` keeps it. A descriptor is only dropped across `execve` if it carries the **`FD_CLOEXEC`** flag. Nothing in the tree set that flag, so every `python3` CGI started life holding a copy of every descriptor the server had open at the moment of the fork: the listen socket(s), the epoll fd, the config file, and **every other client's connected socket and every other in-flight CGI's pipe ends**.

`setup_cgi()` does close descriptors in the child — but only its own four pipe ends (`ClientConnection.cpp:855-860`):

```cpp
if (pid == 0) {
    dup2(stdin_fd[0], STDIN_FILENO);
    close(stdin_fd[0]);
    close(stdin_fd[1]);
    dup2(stdout_fd[1], STDOUT_FILENO);
    close(stdout_fd[0]);
    close(stdout_fd[1]);
    ...
    execve(...);
}
```

That is why the bug is invisible with one request at a time and only appears under concurrency: **a child cleans up after its own request and nothing else.** Request B's child inherits request A's descriptors, which it has no idea exist.

## 2. Consequences

Three, in descending severity.

### 2.1 A completed upload hangs (the one that actually breaks a request)

For a POST to a CGI, the server writes the body into `_cgi_stdin_fd` and then **closes it to signal end-of-body** — that close is the only EOF the script will ever see on stdin.

A pipe delivers EOF when the *last* write end closes. If request B forked while A's stdin write end was open, B's child holds a copy. The server closes its own copy, the pipe still has a writer, and **A's CGI blocks forever in `read()`** waiting for a body that was fully delivered. No bug in either script; the trigger is simply two concurrent CGI requests, one of them a POST.

### 2.2 Closing a client connection does not close it

`delete_conn` closes the socket, but TCP only sends FIN when the last reference to the description drops. A CGI holding an inherited copy keeps the connection half-alive, so the peer never sees the close.

This is worse than it sounds here because **a CGI response without `Content-Length` is close-delimited** — the close *is* the end-of-body marker. The browser sits waiting for a response the server considers finished and sent.

### 2.3 The port stays bound after the server exits

An orphaned CGI holding the listen socket keeps the address in use, so a restart fails with `bind() failed: Address already in use`. `SO_REUSEADDR` (`utils.cpp:26`) does **not** help: it covers `TIME_WAIT`, not a live process still holding a listening socket.

This is how the bug was found. The new `cgi-lifecycle-test` namespace deliberately leaves long-running CGI children alive; the test fixture stops the server with `.destroyForcibly` (SIGKILL), so `~EpollLoop` never runs and never kills the children it tracks. The orphans kept port 8080 bound, and **every test namespace scheduled after it failed to start its server** — a suite-wide failure with no connection to the code under test:

```
LISTEN 127.0.0.1:8080  users:(("python3",pid=205921,fd=4),("python3",pid=205919,fd=4),
                               ("python3",pid=205918,fd=4), ... )   # 10 orphans, no server
```

## 3. `F_SETFD` vs `F_SETFL`

The two `fcntl` commands look interchangeable and are not. They address **different flag words, stored in different places**:

| | `F_SETFD` — descriptor flags | `F_SETFL` — file status flags |
|---|---|---|
| Contains | `FD_CLOEXEC` (the only one) | `O_NONBLOCK`, `O_APPEND`, … |
| Lives on | the **descriptor** — this process's fd-table entry | the **open file description** — the shared kernel object |
| Survives `dup`/`fork`? | each descriptor has its own copy | shared by every `dup` of it |

So setting one never disturbs the other: `FD_CLOEXEC` and `O_NONBLOCK` are two independent `fcntl` calls on the same fd, and adding the former does not replace or remove the latter.

That separation is also *why* the fix is safe. `O_NONBLOCK` sits on the shared description and would follow the descriptor into the child; `FD_CLOEXEC` is per-descriptor, so marking our copy has no effect on anything the CGI legitimately uses.

**No read-modify-write is needed for `F_SETFD`.** `set_nonblocking` carefully does `F_GETFL` and ORs, because `F_SETFL` replaces the whole status word. `F_SETFD` replaces its word too — but `FD_CLOEXEC` is the only flag that word can hold, so `fcntl(fd, F_SETFD, FD_CLOEXEC)` is complete as written.

**`dup2` clears `FD_CLOEXEC` on its destination.** This is what makes marking the pipe ends safe: the child's fd 0 and fd 1 are `dup2` results, so they survive `execve` regardless of how the source descriptors were flagged. Verified in the child's fd table below.

## 4. Fixes applied

Set the flag **at the point of creation**, not before each `fork()`. `setup_cgi` then needs no knowledge of what else is open, and any descriptor added later inherits the habit.

| Descriptor | Site | Fix |
|---|---|---|
| Listen socket | `utils.cpp:24` | `socket(AF_INET, SOCK_STREAM \| SOCK_NONBLOCK \| SOCK_CLOEXEC, 0)` |
| epoll fd | `EpollLoop.cpp:34` | `epoll_create1(EPOLL_CLOEXEC)` |
| CGI pipes (×4) | `ClientConnection.cpp:831-832, 841-842` | `fcntl(fd, F_SETFD, FD_CLOEXEC)` after each `pipe()` |
| Accepted client socket | `ServerConnection.cpp:23` | **still open — see §6** |

`set_cloexec()` was added to `utils.cpp:18` alongside `set_nonblocking`, throwing on failure to match its neighbour:

```cpp
void	set_cloexec(int fd) {
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
		throw std::runtime_error(std::string("fcntl F_SETFD failed: ") + strerror(errno));
}
```

Inside `setup_cgi` the return value is ignored instead, matching the existing `F_SETFL` calls two lines below (`:867-868`): the code is mid-fork-setup with pipes already open, and `F_SETFD` can realistically only fail with `EBADF` on a descriptor just created.

**Placement of the pipe calls.** Immediately after each `pipe()` succeeds and before `fork()` — the earliest safe point. Only the two ends the parent *keeps* can be inherited by a later fork in a single-threaded server, but marking all four is simpler and obviously correct. (In a threaded program the gap between `pipe()` and `fcntl` would itself be a race; that is what `pipe2(O_CLOEXEC)` exists for. Not needed here, and `pipe2` is not on the allowed list.)

## 5. Verification

Live inspection of a CGI child's descriptor table via `/proc`, with the server holding `fd 4 = socket:[1448631]` (the listen socket) and `fd 5 = anon_inode:[eventpoll]`:

```
--- CGI child fds ---
  fd 0 -> pipe:[1473664]              <- its own stdin, via dup2 (correct)
  fd 1 -> pipe:[1473663]              <- its own stdout, via dup2 (correct)
  fd 2 -> <server's stderr>           <- inherited by design (CGI errors reach the log)
  fd 3 -> conf/generated/base.conf    <- see §7
  fd 6 -> socket:[1473662]            <- a CLIENT socket: still leaking
```

The listen socket and the epoll fd are **absent** — `SOCK_CLOEXEC` and `EPOLL_CLOEXEC` work. Killing the server with SIGKILL while an orphan CGI survives now leaves the port free, so §2.3 is closed.

Cross-request check, two concurrent CGI requests A and B:

```
CGI A=219481  its client socket: socket:[1471757]
CGI B=219504 fds:  ... fd 6 -> socket:[1471757]
>>> YES: fd 6 in CGI B == client A's socket
```

B holds A's **socket** but **not A's pipes** — confirming the pipe fix works and isolating the one remaining gap. §2.1 (the stdin-EOF hang) is therefore closed; §2.2 is not.

## 6. Still open: the accepted client socket

`set_cloexec()` is declared (`utils.hpp:8`) and defined (`utils.cpp:18`) but **never called**. The accept path still marks the new socket non-blocking only:

```cpp
int client_fd = accept(fd, (sockaddr*)&client_addr, &client_len);
if (client_fd < 0) return;
set_nonblocking(client_fd);          // ServerConnection.cpp:23 — needs set_cloexec too
```

One line closes it:

```cpp
set_nonblocking(client_fd);
set_cloexec(client_fd);
```

`ServerConnection.cpp:23` is the only place an accepted socket is created, so that call site is complete. Folding the flag into `set_nonblocking` itself would work — its sole caller is this line — but the name would then lie about what it does, hence the separate function.

Until this lands, consequence §2.2 stands: a CGI forked during one request holds another client's socket, so closing that connection does not send FIN, and a close-delimited CGI response can leave the browser waiting.

## 7. Deliberately not changed

- **`std::fstream` members** (`_stream` in `ClientConnection`, `Lexer::stream` at `ConfigParser.hpp:122`). C++98 gives no portable access to the underlying descriptor, so they cannot be marked. This is why `fd 3 -> base.conf` still appears in the child's table: the lexer's `ifstream` is a member that is never closed after parsing, so it stays open for the process lifetime. Both are regular files — they pin an inode, not a port or a peer connection — so the cost is bounded. Closing the lexer stream after parsing would remove that one anyway.
- **`fd 2` (stderr).** Inherited on purpose: it is how a CGI's error output reaches the server log. Standard CGI behaviour, not a leak.

## 8. Subject compliance

`fcntl` is on the allowed-functions list (`subject.txt:26`), and this copy of the subject carries no clause restricting it to `F_SETFL`/`O_NONBLOCK`. `SOCK_CLOEXEC` and `EPOLL_CLOEXEC` are flags to `socket()` and `epoll_create` — both allowed — exactly as `SOCK_NONBLOCK` already was.

`accept4()` and `pipe2()`, which would set the flag atomically at creation, are **not** on the list; hence `accept` + `fcntl` and `pipe` + `fcntl`.

## 9. Relationship to the CGI reaper (issues 5 / 9)

The deferred-reaper work bounds this bug but does not fix it. With a `CGI_TIMEOUT` deadline in place, no CGI outlives its deadline plus `CGI_KILL_GRACE`, so a leaked descriptor is held for seconds rather than forever — the port comes back, the FIN eventually goes out. But the stdin-EOF hang of §2.1 still happens *inside* that window, and the reaper's own SIGKILL is what ends it, turning a completed upload into a 502.

The two fixes are complementary: **the reaper bounds the damage, `FD_CLOEXEC` removes the mechanism.**

## 10. Follow-ups

- **Call `set_cloexec(client_fd)`** in `ServerConnection::handle` (§6). One line; the last of the four descriptor classes.
- **Test-suite workaround can then be relaxed.** `kill-stray-children!` / the `reap-strays` fixture in `cgi_lifecycle_test.clj` exist because orphaned children pinned port 8080. With the listen socket marked that no longer happens, so they are now belt-and-braces rather than load-bearing — worth keeping (they still prevent stray processes accumulating across runs) but no longer the difference between a green and a red suite.
- **Close `Lexer::stream` after parsing** (§7). Unrelated hygiene, noticed here.
