## Build Commands

```bash
make          # Build ./webserv (release, -O3)
make DEBUG=1  # Build with -g3 (no -O3)
make re       # Full rebuild
make clean    # Remove obj/
make fclean   # Remove obj/ and webserv
```

There is **no parser submodule** anymore — config parsing is compiled directly from `src/ConfigParser.cpp` and `src/Config.cpp`. The `parser/` directory referenced by older docs/commits is gone on this branch.

> **Known Makefile bug:** `$(ODIR)` (the `obj/` directory) is listed as a normal prerequisite of `$(NAME)`, not an order-only one, so make may try to compile `.o` files before `obj/` exists and fail with "opening dependency file obj/*.d: No such file or directory". Workaround: `mkdir -p obj` once, or change the pattern rule to `$(ODIR)%.o: %.cpp | $(ODIR)`.

Run the server:

```bash
./webserv [configuration file]   # defaults to webserv.conf if no arg
```

### Test configs and assets

Config files under `conf/` use a `__WWWROOT__` placeholder. `make prepare-confs` substitutes the absolute www path and writes runnable copies into `conf/generated/`. Web assets live under `www/`. There is a Clojure test harness wired via `make test` (installs the Clojure CLI to `~/.local`, runs `webserv-tests`).

```bash
make prepare-confs
./webserv conf/generated/base.conf
```

## Constraints

- **C++98 only** — no C++11 or later. No lambdas, no `auto`, no range-for, no `nullptr`.
- **`-Wall -Werror -Wextra`** — all warnings are errors. (Note: the Makefile currently also passes `-Wno-type-limits -Wno-maybe-uninitialized`, marked as a TODO to remove.)
- **Linux only** — uses `epoll`, not `poll` or `select`.

## ⚠️ In-progress refactor: the `.new` files are what's compiled

The build is mid-migration. The Makefile (`SRCS`) compiles the **`.new` variants** and a few classic files:

- Compiled: `webserv.cpp`, `ConfigParser.cpp`, `Config.cpp`, `ServerConnection.cpp`, **`ClientConnection.new.cpp`**, `EpollLoop.cpp`, `ServerBlock.cpp`, **`Request.new.cpp`**, **`Response.new.cpp`**, `ScratchBuffer.cpp`, `utils.cpp`.
- **Not compiled (legacy/dead):** `ClientConnection.cpp`, `Request.cpp`, `Response.cpp`, `RequestParser.cpp`, `CgiConnection.cpp`, `Connection.cpp` (empty). `RequestParser` and `CgiConnection` are commented out in the Makefile — their logic now lives **inline inside `ClientConnection.new.cpp`**.

When editing connection/request/response logic, edit the **`.new`** files. The non-`.new` files (e.g. `inc/ClientConnection.hpp`, `inc/Response.hpp`, `inc/RequestParser.hpp`) are stale.

> **Latent ODR bug (currently masked):** `src/ServerConnection.cpp` includes the *old* `inc/ClientConnection.hpp` (1616-byte class) to get a declaration, but the actual constructor/vtable it links against comes from `ClientConnection.new.cpp` (984-byte class). Two different definitions of `class ClientConnection` across translation units is undefined behavior. It happens to "work" today only because `new ClientConnection(...)` over-allocates (old size > new size). Fix by pointing `ServerConnection.cpp` at `ClientConnection.new.hpp`.

## Architecture

### Event Loop

`EpollLoop` is a singleton (`get_instance()`) that owns all active connections as `map<int, Connection*>` (fd → Connection). `run()` calls `epoll_wait()` with an **infinite timeout (`-1`)**, dispatches each ready event to `conn->handle(events)`, then flushes the `_deletion_queue` at the end of the tick (connections are queued for deletion during event handling rather than freed immediately, to avoid dangling pointers within the same batch).

SIGINT sets a `sig_int` flag that stops the loop cleanly. `main()` also installs `SIG_IGN` for SIGPIPE so writes to broken CGI/client pipes don't kill the server.

- `add(conn)` — register `conn->fd` with `EPOLLIN | EPOLLERR | EPOLLHUP`
- `mod(conn, events)` — replace the watched event mask on the *same* fd
- `rearm(conn, events, new_fd)` — **EPOLL_CTL_DEL the old fd, swap `conn->fd` to `new_fd`, and re-ADD with a new mask.** This is how a single `ClientConnection` migrates between watching the client socket, the CGI stdin pipe, and the CGI stdout pipe over its lifetime.
- `del(conn)` — enqueue for deletion; deregistered, closed, and freed in `clear()` at end of tick

> Known gaps flagged in-source: `epoll_wait` `EINTR` is not handled; an event later in the same batch can dereference a `Connection` already `del()`'d earlier in the batch (no `_connections.count` re-check before dispatch).

### Connection Hierarchy

```
Connection (abstract; owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
└── ClientConnection  — the whole request→response lifecycle (parsing, CGI, response)
```

`CgiConnection` no longer exists in the compiled build — CGI is driven entirely from within `ClientConnection` via `rearm()`. `ServerConnection::handle()` does a **single** `accept()` per readiness event (the multi-accept loop is commented out).

### ClientConnection — the state machine

`ClientConnection` (`ClientConnection.new.{hpp,cpp}`) is the heart of the server. It holds a `ScratchBuffer _buf`, a `Request _req`, a `Response _res`, an `std::fstream _stream` (for the file/CGI-temp body), CGI fds/pid, and a `_state`:

```
REQ_LINE → REQ_HEADERS → REQ_SETUP → ┐
                                       ├─→ RESPONSE                (static GET / errors / redirects)
                                       └─→ REQ_BODY → CGI_TRANSMIT_BODY → CGI_HEADERS → CGI_BODY → RESPONSE  (CGI)
(AUTOI_HEADERS / AUTOI_BODY states exist but are unused — autoindex is a stub)
```

`handle(events)` dispatches by state:
- Body-to-CGI states (`REQ_BODY`, `CGI_TRANSMIT_BODY`) → `handle_cgi_input()`
- CGI-output states (`CGI_HEADERS`, `CGI_BODY`) → `handle_cgi_output()`
- Otherwise, on `EPOLLIN`: fill `_buf` from the socket, then run `handle_req_line()` → `handle_req_headers()` → `handle_setup()` as the buffer accumulates.
- On `EPOLLOUT` in `RESPONSE`: `handle_response()` drains buffered headers + file to the socket; returns true when done → `epoll.del(this)`.

Key steps:
1. **Request line** (`handle_req_line`): validates method token / URI / version, normalizes the path (`.`/`..` collapsing), percent-encoding check; sets `_req.status` (400/501/505/414 on failure).
2. **Headers** (`handle_req_headers` + `parse_req_headers`): lowercases keys, extracts `Host`→`hostname`/`port`, `Content-Length`; 411 if POST has no length; 431 if headers overflow the buffer.
3. **Setup** (`handle_setup`): resolves virtual server via `Host`, walks the `Location` tree applying internal redirects, `return` directives, `index`, `autoindex`, method-allow and file-existence checks, in a loop bounded by `REDIRECT_LIMIT` (default 5). Error pages are resolved via `epi_redirect()` (internal redirect to the configured `error_page`, or a body-less response).
4. **CGI** (`setup_cgi`): `pipe()`×2 + `fork()` + `execve(interpreter, [interpreter, script], envp)`. Env includes `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `REQUEST_METHOD`, `SCRIPT_FILENAME`, `PATH_INFO`, `QUERY_STRING`, `SERVER_NAME`, plus any `cgi_param` pairs. The pipe ends are made non-blocking and the connection `rearm()`s onto them. POST bodies are streamed from the client buffer to CGI stdin; CGI stdout is buffered to a temp file (`/tmp/cgi_XXXXXX`), then `parse_cgi_headers()` splits headers (incl. a `Status:` override) from the body and `finalize_cgi()` `waitpid()`s and builds the response.
5. **Static response** (`setup_res`): builds status line + headers (`Location`/`Allow`, `Date`, `Content-Length` from `stat`), opens the file via `_stream`, `mod()`s to `EPOLLOUT`. `setup_internal_error()` swaps in a static 500.

### ScratchBuffer

`ScratchBuffer` is a fixed-capacity byte buffer with two cursors: `readpos` (write-in / produce position) and `writepos` (consume position). `fill(...)` appends data **into** the buffer (from an fd, an `fstream`, or a literal); `feed(...)` drains data **out** of the buffer (to an fd or `fstream`). `fill_capacity()` is space remaining for `fill`; `feed_capacity()` is bytes pending to `feed`. `find(char/string)` searches the produced region; `erase(from,to)` memmoves to drop a span; `clear()` resets both cursors. `set_data()` lets it *reference* an external buffer (used for the static 500 string) instead of owning a heap buffer (`_ref_data`).

> Capacity comes from the matched server's `client_header_buffer_size`. A request line / header block larger than this buffer yields 414/431 rather than growing.

### Request

`Request` (`Request.new.hpp`) fields: `method` (`HttpMethod` enum), `uri` (raw), `path` (normalized/decoded), `query`, `version`, `headers` (map, lowercased keys), `content_length`, `host`/`hostname`/`port`, `status` (the current HTTP status being built toward), and the booleans `internal` (path is internal vs. an external URL) and `no_file` (build a body-less response).

### Response

`Response` (`Response.new.hpp`) is a thin **header-list builder**, not a writer: it owns a `std::vector<std::string> headers` queue. Builder methods: `add_status_line(version, code)` (looks up the reason phrase from a static map), `add_header_field(name, value)` / `(name, size_t)`, `add_allowed(loc)` (emits an `Allow:` list from the location's `limit_except`), `add_date()`, `add_header_end()`. The actual socket writing is done by `ClientConnection` (`buffer_res_headers()` / `buffer_file()` → `_buf.feed(fd)`); the file body is streamed through `_stream` + `_buf`.

### Configuration

`inc/Config.hpp` (+ `src/Config.cpp`) defines the config object tree: `Http` → `Server` → `Location`, plus `Port` and the `config::*` structs (`listen`, `redirect`, `index`, `autoindex`, `cgi`, `errorpageinfo`, `limit`, `mime`, etc.). `inc/ConfigParser.hpp` (+ `src/ConfigParser.cpp`) provides the nginx-style tokenizer/grouper: `Lexer` → `Grouper` (with `MainDirective`/`BodyDirective`/`SimpleDirective`). `main()` does `Grouper grouper(path); grouper.group();` then `Http http(grouper.main.body_directives[0])`.

Config hierarchy: `http {}` → `server {}` → `location {}`, with outer-scope directives inherited. Virtual-host resolution: `Http::get_default_server(sockaddr_in)` picks the listen-address default; `Http::get_server(sockaddr_in, hostname)` refines by `server_name`. `Server::get_location(uri)` / `Location::get_location(uri)` resolve the route. Accessors used by `ClientConnection` include `get_root()`, `get_limit()`, `get_cgi()`, `get_redirect()`, `get_index()`, `get_autoindex()`, `get_errorpages()`, `get_header().buffer_size`.

Key directives:

| Directive | Scope | Notes |
|---|---|---|
| `listen <ip>:<port> [default_server] [backlog=N]` | server | multiple allowed |
| `server_name <name>...` | server | virtual host selection |
| `root <path>` | http/server/location | |
| `error_page <code>... <uri>` | http/server/location | optional response-code override; can redirect internally or to an external URL |
| `return <code> <url\|path>` | location | redirect (`redirect` struct) |
| `index <file>` | http/server/location | default file for a directory |
| `autoindex on\|off` | http/server/location | **directory listing is currently a no-op stub** |
| `client_max_body_size <size>` | http/server/location | k/m suffixes |
| `client_header_buffer_size`, `large_client_header_buffers` | http/server | sizes the `ScratchBuffer` |
| `client_body_buffer_size`, `client_body_timeout` | http/server/location | |
| `output_buffers` | http/server/location | |
| `types { <mime> <ext>...; }` / `default_type <mime>` | http/server/location | |
| `limit_except <method>... { ... }` | location | allowed methods |
| `cgi_pass <interpreter>` | location | interpreter path; enables CGI for the location |
| `cgi_param <key> <value>` | location | extra env vars for the CGI process |

### Logger

`Logger` is a singleton with levels `LOG_DEBUG < LOG_INFO < LOG_WARN < LOG_ERROR`, set at startup from the `LOG_LEVEL` env var (`DEBUG`/`INFO`/`WARN`/`ERROR`), defaulting to `LOG_DEBUG`. Use:

```cpp
LOG_INFO("component") << "message" << std::endl;
```

### Data Flow

1. `main()` parses config → builds `Http` → one `ServerConnection` per `Port` entry (`make_server_socket`) → `EpollLoop::run()`.
2. Accept: `ServerConnection::handle()` → `accept()` → `new ClientConnection(fd, &http, addr)` → `epoll.add()`.
3. Read/parse: `ClientConnection::handle(EPOLLIN)` fills `_buf`, runs the `REQ_LINE → REQ_HEADERS → REQ_SETUP` chain.
4. Setup decides: static `RESPONSE`, or CGI (`setup_cgi` forks, `rearm()`s onto the CGI pipes).
5. CGI: stream client body → CGI stdin; CGI stdout → temp file; parse headers; `waitpid`; `rearm()` back to the client fd in `RESPONSE`.
6. Write: `handle(EPOLLOUT)` in `RESPONSE` drains headers + file via `_buf.feed(fd)`; on completion → `epoll.del(this)`.
7. Error/hangup at any stage: `epoll.del(conn)` → closed and freed at end of tick.

## Source Layout

- `src/` — compiled set listed above. `.new.*` files are the live implementations; the matching non-`.new` files are stale legacy.
- `inc/` — headers (note the stale `ClientConnection.hpp`, `Response.hpp`, `Request.hpp`, `RequestParser.hpp`, `ScratchBuffer.hpp` vs. live `*.new.hpp`), plus `Logger.hpp`, `Config.hpp`, `ConfigParser.hpp`.
- `conf/` — example configs (use `__WWWROOT__`); `conf/generated/` holds `make prepare-confs` output.
- `www/` — static assets, error pages, `cgi-bin/`.
- `webserv-tests/` — Clojure integration test suite (`make test`).
- `incremental_versions/` — historical snapshots, not compiled.

## Subject scope & known gaps

The assignment (`subject.txt`) is the 42 *webserv*: a non-blocking C++98 HTTP server driven by a single `epoll`, never reading/writing a socket/pipe without prior readiness, never checking `errno` after read/write, must not crash, GET/POST/DELETE, file upload, configurable error pages / body size / routes / redirects / directory listing / CGI by extension, multiple listen ports.

Gaps/bugs observed in the current code that are worth knowing before extending it:

- **Parse-error status is clobbered.** On a malformed request line/headers, `handle_req_line`/`handle_req_headers` set `_req.status` (e.g. 400) and route to `REQ_SETUP`, but `handle_setup()` unconditionally resets `_req.status` to 200 (or 201 for POST). Result: malformed requests like `GET\r\n\r\n` currently return **200**. Parse errors should bypass setup and go straight to building the error response.
- **DELETE doesn't delete.** There is no `unlink`/`remove`; DELETE is treated like GET (serves the file).
- **File upload (non-CGI POST) is unimplemented.** `is_method_allowed()` rejects POST unless the location has `cgi_pass`; POST bodies are only ever streamed to a CGI process, never written to disk at an upload location.
- **`autoindex` is a stub** — `setup_autoindex()` returns true without generating a listing; the `AUTOI_*` states are unused.
- **Chunked transfer-encoding is not handled** — only `Content-Length` bodies are read; `Transfer-Encoding: chunked` requests are not un-chunked.
- **`is_dir()` is wrong** — compares `statbuf.st_mode == S_IFDIR` instead of `S_ISDIR(statbuf.st_mode)`, so it effectively never detects directories.
- **`handle_req_headers` colon check** uses `if (!colon)` (true only when the colon is at index 0); a header line with no colon is not rejected.
- **`handle_cgi_output` hangup check** is `events & (EPOLLHUP | EPOLLHUP)` — a duplicated flag; it likely meant `EPOLLHUP | EPOLLERR`.
- **Duplicate request headers** are silently dropped (`FIXME` in source).
