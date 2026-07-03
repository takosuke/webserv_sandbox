# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. For a fuller narrative and the running list of known bugs, see `Architecture.md`.

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

Solid: single-`epoll` I/O, no `errno` after read/write, SIGPIPE ignored, config parsing + virtual hosts, redirect/error-page loop, CGI pipe/`rearm` machine, correct `is_dir()`, working directory listing.
