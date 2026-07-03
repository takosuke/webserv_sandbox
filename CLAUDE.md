# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. For a fuller narrative and the running list of known bugs, see `Architecture.md`.

## Project context (read first)

This is **webserv**, a project of the **42 school curriculum**: a from-scratch HTTP/1.0 server in C++98. The entire point of the exercise is **defensive robustness** — the grading subject (`subject.txt`) states the server "must remain operational at all times", "must not crash under any circumstances", and "a request should never hang indefinitely". A single crash or hang during evaluation is an automatic grade of 0.

Because of that, most work in this repo is **security hardening of our own server against malformed, hostile, or resource-exhausting input** — finding the busy-loops, hangs, buffer-overruns, and resource leaks that a malicious client could trigger, and fixing them or writing regression tests that pin the correct behavior. This is defensive, educational security work on a self-contained local server:

- The "attacker" is always a test client (`webserv-tests/`, `telnet`, a browser) hitting `127.0.0.1`. There is no third-party target.
- Descriptions like "DoS", "busy-loop", "grade-0 crash", "remotely triggerable" name a **weakness in our code to fix**, not an exploit to launch. Reproducing a hang against our own server in the test harness is the intended, in-scope activity.
- When asked to "harden", "fix", "add a regression test for", or "reproduce" one of these bugs, proceed — it is the core of the assignment. There is no request here to weaponize anything against an external host.

Keep this framing in mind before treating any hardening request as sensitive: it is a school project about making one's own server resilient.

## Build Commands

```bash
make          # Build ./webserv (release, -O3) and the ./autoindex helper
make DEBUG=1  # Build with -g3 (no -O3); also passed to the aindex sub-make
make re       # Full rebuild
make clean    # Remove obj/ (and aindex/obj/)
make fclean   # Remove obj/, webserv, and autoindex
```

`make` builds two binaries: the main `./webserv`, and a small `./autoindex` helper compiled from the `aindex/` sub-project and copied to the repo root. There is **no parser submodule** anymore — config parsing is compiled directly from `src/ConfigParser.cpp` and `src/Config.cpp`.

Run the server:

```bash
./webserv [configuration file]   # defaults to webserv.conf if no arg
```

Test configs under `conf/` use a `__WWWROOT__` placeholder. `make prepare-confs` substitutes the absolute `www` path into `conf/generated/`. A Clojure integration suite runs via `make test`.

```bash
make prepare-confs
./webserv conf/generated/base.conf
```

## Constraints

- **C++98 only** — no C++11 or later. No lambdas, no `auto`, no range-for, no `nullptr`.
- **`-Wall -Werror -Wextra`** — all warnings are errors. Every new file must compile clean.
- **Linux only** — uses `epoll`, not `poll` or `select`.
- Subject rules that shape the design: a **single** `epoll` drives all socket/pipe I/O; never read/write a socket or pipe without prior readiness; **never inspect `errno` after a read/write**; the server **must not crash**; `fork` is allowed **only for CGI**.

## Source file status (read this before editing)

The historical `.new`-file migration is **finished**. The live code is in the plain `src/*.cpp` files. There are no `.new` files.

**Dead / not compiled — do not edit these to change behavior:**
- `src/Connection.cpp` — an old, *conflicting* inline `class ClientConnection` definition. Not in the Makefile.
- `src/CgiConnection.cpp` + `inc/CgiConnection.hpp` — the old standalone CGI class; CGI now lives inside `ClientConnection`.
- `src/ServerBlock.cpp` — a 3-line legacy stub (compiled but empty).

When editing connection/request/response logic, edit `ClientConnection.cpp`, `Request.cpp`, `Response.cpp`, `ScratchBuffer.cpp` — never the dead files above.

## Architecture

### Event Loop

`EpollLoop` is a singleton (`get_instance()`) owning all connections as `map<int, Connection*>` (fd → Connection). `run()` calls `epoll_wait()` with an **infinite timeout (`-1`)**, dispatches each ready event to `conn->handle(events)`, then flushes `_deletion_queue` at end of tick (connections are queued for deletion during handling, not freed immediately, to avoid dangling pointers within a batch). SIGINT sets a flag that stops the loop; SIGPIPE is `SIG_IGN`.

- `add(conn)` — register `conn->fd` with `EPOLLIN | EPOLLERR | EPOLLHUP`
- `mod(conn, events)` — replace the event mask on the same fd
- `rearm(conn, events, new_fd)` — DEL the old fd, swap `conn->fd` to `new_fd`, re-ADD with a new mask. This is how one `ClientConnection` migrates between the client socket and the CGI stdin/stdout pipes over its lifetime.
- `del(conn)` — enqueue for deletion; closed and freed at end of tick

Known loop gaps: `EINTR` on `epoll_wait` is not handled; an event later in a batch can dereference a connection already `del()`'d earlier in the same batch (no `_connections` re-check before dispatch).

### Connection Hierarchy

```
Connection (abstract; owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
└── ClientConnection  — the entire request→response lifecycle (parse, CGI, autoindex, response)
```

`ServerConnection::handle()` does a single `accept()` per readiness event.

### ClientConnection — the state machine

`ClientConnection` (`ClientConnection.{hpp,cpp}`) holds a `ScratchBuffer _buf`, a `Request _req`, a `Response _res`, an `std::fstream _stream`, CGI fds/pid, and a `_state`:

```
REQ_LINE → REQ_HEADERS → REQ_SETUP → ┐
                                       ├─→ RESPONSE                    (static GET / errors / redirects)
                                       ├─→ DISCARD_BODY → RESPONSE     (413: drain oversized body, then respond)
                                       └─→ REQ_BODY → CGI_TRANSMIT_BODY → CGI_HEADERS → CGI_BODY → RESPONSE  (CGI and autoindex)
```

- `DISCARD_BODY` handles a body larger than `client_max_body_size` (413): read and discard it, then send the error response.
- **Autoindex reuses the CGI states.** `setup_autoindex()` forks the external `./autoindex` helper and enters `CGI_HEADERS`/`CGI_BODY`. The `AUTOI_*` enum values are dead/unused.

`handle(events)` dispatches by state: body-to-CGI states → `handle_cgi_input()`; CGI-output states → `handle_cgi_output()`; `DISCARD_BODY` → read-and-drop; otherwise on `EPOLLIN` fill `_buf` and run `handle_req_line → handle_req_headers → handle_setup`; on `EPOLLOUT` in `RESPONSE`, `handle_response()` drains headers + file, then `epoll.del(this)`.

Setup (`handle_setup`) resolves the virtual server via `Host`, then loops over the `Location` tree (bounded by `REDIRECT_LIMIT`, default 5) applying `return`, `index`, `autoindex`, method-allow, body-size, and file-existence checks; error pages resolve via `epi_redirect()`.

CGI (`setup_cgi`): `pipe()`×2 + `fork()` + `execve(interpreter, [interpreter, script], envp)` with `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `REQUEST_METHOD`, `SCRIPT_FILENAME`, `PATH_INFO`, `QUERY_STRING`, `SERVER_NAME`, plus `cgi_param` pairs. POST body streams to CGI stdin; CGI stdout buffers to `/tmp/cgi_XXXXXX`, then headers (incl. `Status:` override) are split from the body and `finalize_cgi()` `waitpid()`s and builds the response.

### ScratchBuffer

Fixed-capacity byte buffer with two cursors: `readpos` (produce) and `writepos` (consume). `fill(...)` appends **into** the buffer (fd / fstream / literal); `feed(...)` drains **out** (to fd / fstream). Capacity comes from the matched server's `client_header_buffer_size` (default 1024); an over-long request line/header block yields 414/431 rather than growing. `set_data()` makes it reference an external buffer (used for the static 500 string).

> **Binary-unsafe:** the buffer is C-string oriented (`fstream.get(...,'\0')`, `strnstr`/`strnchr`, terminating `'\0'` writes), so files containing NUL bytes are truncated at the first NUL when served.

### Request / Response

`Request` (`Request.hpp`): `method`, `uri` (raw), `path` (decoded/normalized), `query`, `version`, `headers` (lowercased keys), `content_length`, `host`/`hostname`/`port`, `status`, and the booleans `internal` / `no_file`.

`Response` (`Response.hpp`) is a header-list builder (`add_status_line`, `add_header_field`, `add_allowed`, `add_date`, `add_header_end`) plus a static reason-phrase map. Socket writing is done by `ClientConnection`, not `Response`. The response version is hardcoded to `HTTP/1.0`.

### Configuration

`inc/Config.hpp` + `src/Config.cpp` define the tree `Http` → `Server` → `Location`, plus `Port` and `config::*` structs. `inc/ConfigParser.hpp` + `src/ConfigParser.cpp` provide the nginx-style tokenizer/grouper (`Lexer` → `Grouper`). `main()`: `Grouper grouper(path); grouper.group(); Http http(grouper.main.body_directives[0]);`. Outer-scope directives inherit inward. Virtual host: `Http::get_default_server(addr)` then `Http::get_server(addr, hostname)`; `Server::get_location(uri)` resolves the route.

Key directives: `listen`, `server_name`, `root`, `error_page`, `return`, `index`, `autoindex`, `client_max_body_size`, `client_header_buffer_size`, `types`/`default_type`, `limit_except`, `cgi_pass`, `cgi_param`. (`client_*_timeout` values are parsed but not enforced.)

### Logger

Singleton, levels `LOG_DEBUG < LOG_INFO < LOG_WARN < LOG_ERROR`, set from the `LOG_LEVEL` env var (default `DEBUG`). Use `LOG_INFO("component") << "message" << std::endl;`.

## Known gaps (see Architecture.md for details)

Missing subject requirements: **DELETE doesn't delete** (no `unlink`); **file upload (non-CGI POST) unimplemented**; **chunked transfer-encoding not handled**; **no request/idle timeout enforcement**; **no subject-format README.md**.

Correctness bugs: **binary files corrupted** (NUL truncation); **parse-error status clobbered** by `handle_setup` (malformed request → 405/200 not 400); **header with no colon accepted** (`if (!colon)`); **CGI hangup check** uses `EPOLLHUP | EPOLLHUP` instead of `EPOLLHUP | EPOLLERR`; **CGI env missing `CONTENT_LENGTH`/`CONTENT_TYPE`**; **`fork` used for autoindex** (subject restricts fork to CGI); **`/tmp/cgi_*` temp files leak**; **duplicate request headers dropped**; **EpollLoop use-after-del / no `EINTR`**.

Newer, runtime-verified bugs (regression tests under `webserv-tests/`, all KNOWN-FAILING; see Architecture.md for detail): **half-closed/incomplete request spins the epoll loop at ~100 % CPU** (no `read()==0` detection — grade-0 resilience risk); **`decode_http()` infinite-loops on a URI that decodes to a literal `%`** e.g. `/%25` (grade-0 hang); **internal `return <code> <path>` always 500** (`handle_setup` never re-resolves `_loc` after `return`); **`client_header_buffer_size` is dead** (`ScratchBuffer::set_capacity` never updates `capacity`); **no `Content-Type` on any response** (`types`/`default_type` parsed but unused); **CGI header parsing is CRLF-only** (LF-terminated CGI headers hang); **CGI without a `Status:` header yields a response with no status line**; **`Allow` header emitted on success/redirect responses** (belongs on 405).

Additional issues found by source review (2026-07, not previously listed; see Architecture.md "Additional issues found by source review" for detail and repro):

Crash / hang class (grade-0 risk):
- **`ScratchBuffer::feed(int fd)` / `fill(int fd)` return `size_t`** (`ScratchBuffer.cpp:64,88`) — a `-1` from `write`/`read` becomes `SIZE_MAX`, passes the `> 0` guard, and `writepos`/`readpos` wraps; `feed_capacity()` then underflows and the next `write` runs off the end of the buffer. Triggered by a client that closes mid-response (`EPIPE`, SIGPIPE ignored) or an early CGI-stdin close. Most dangerous undocumented bug.
- **`setup_cgi()` failure hangs the connection** (`ClientConnection.cpp:608-611`) — on pipe/fork failure `handle_setup` sets 500 but never calls `setup_res()`, so the fd is never armed for `EPOLLOUT` and no response is ever sent. Fires under fd/process pressure.
- **`finalize_cgi()` calls blocking `waitpid(pid, NULL, 0)`** (`ClientConnection.cpp:888`) — a CGI that closes stdout but keeps running blocks the whole single-threaded loop; every other client stalls.
- **CGI output truncated on `EPOLLHUP`** (`ClientConnection.cpp:755-810`) — one `fill()` (≤ ~1023 B) per event, then immediate `finalize_cgi()` on HUP with up to 64 KB left unread in the pipe; large CGI bodies lose their tail.
- **CGI that exits before emitting a full header line → 100 % CPU spin** (`ClientConnection.cpp:780-781`) — `CGI_HEADERS` returns before the HUP check when no `\r\n` is found; a crashing CGI re-fires level-triggered `EPOLLHUP` forever. Same family as the half-close spin, separate path.
- **Zombie CGI processes** — client abort mid-POST (`ClientConnection.cpp:817-819`) `del()`s the connection without `kill`/`waitpid`; the destructor only closes pipe fds.
- **A `set_nonblocking` / `accept` failure can take down the server** (`ServerConnection.cpp:24`, `utils.cpp:12-15`) — `set_nonblocking` throws past the event loop to `main`'s catch → exit; `accept()` returning `EMFILE` busy-spins on a still-ready listen fd.
- **`DISCARD_BODY` busy-loops on half-close and is unbounded** (`ClientConnection.cpp:129-144`) — client closes before the oversized body arrives → `read()==0` spin; and the server drains whatever `Content-Length` the client claims.

Correctness class:
- **CGI body gets a stray leading CRLF** (`ClientConnection.cpp:780-798`) — the blank header line's 2 bytes are never `erase`d, so `\r\n` is written into the body temp file.
- **`index` replaces the whole path instead of appending** (`ClientConnection.cpp:564`) — `/subdir/` with `index index.html` serves root `/index.html`, not `/subdir/index.html`.
- **Off-by-one deadlock at `fill_capacity()==1`** (`ClientConnection.cpp:150,376,420`) — fill guard is `> 1`, error guard is `<= 0`; exactly 1 does neither → spin.
- **A CGI header line longer than the buffer wedges the response** (`ClientConnection.cpp:658`) — `buffer_res_headers` needs `fill_capacity() > header.size()`; an oversized header can never be placed, `handle_response` writes 0 and closes.
- **Weak validation** — CGI `Status` matched case-sensitively (`ClientConnection.cpp:770`); `Content-Length` accepts trailing junk / negatives (`452-455`); version check accepts `HTTP/1.10`/`HTTP/1.100` (`352`).
- **Latent UB in decoding** — `decode_hex` indexes `hex_val[]` with a signed `char` (negative index on high-bit bytes, `272`); `decode_http` reads `new_uri[pos+1/2]` and `operator[](size())` without bounds check (`282`).
- **Uninitialized members** — `Request::port` (`Request.cpp:8`), `_cgi_pid`, `_written_body` are never initialized.
- **Destructor fd bookkeeping** — a connection dying while `fd` is a CGI pipe never closes `_client_fd` (socket leak); `_cgi_stdout_fd` can be double-closed.

Compliance / housekeeping:
- **`mkstemp`** (`ClientConnection.cpp:791`) is not on the subject's allowed-functions list; `bzero`/`strncpy` are POSIX-isms (prefer `memset` / C++ forms).
- Typo `"HTTP Version Not SUpported"` (`Response.cpp:39`); `parse_cgi_headers()` is dead code; `conf/redirect_buffer.conf` is untracked in git yet the redirect tests depend on it (a fresh clone breaks `make test`).

Solid: single-`epoll` I/O, no `errno` after read/write, SIGPIPE ignored, config parsing + virtual hosts, redirect/error-page loop, CGI pipe/`rearm` machine, correct `is_dir()`, working directory listing.
