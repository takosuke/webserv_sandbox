## Build Commands

```bash
make          # Build ./webserv (release, -O3) and the ./autoindex helper
make DEBUG=1  # Build with -g3 (no -O3); passed through to the aindex sub-make too
make re       # Full rebuild
make clean    # Remove obj/ (and aindex's obj/)
make fclean   # Remove obj/, webserv, and autoindex
```

`make` builds two binaries: the main `./webserv`, and a small `./autoindex` helper compiled from the `aindex/` sub-project (`make -C ./aindex`, then the binary is copied to the repo root). There is **no parser submodule** — config parsing is compiled directly from `src/ConfigParser.cpp` and `src/Config.cpp`.

Run the server:

```bash
./webserv [configuration file]   # defaults to webserv.conf if no arg
```

### Makefile notes

- `$(ODIR)` (`obj/`) is a normal prerequisite of `$(NAME)` (listed before `$(OBJS)`), not an order-only one. The build currently succeeds because it is listed first, but if you reorder prerequisites you may hit "opening dependency file obj/*.d: No such file or directory". Safer form: `$(ODIR)%.o: %.cpp | $(ODIR)`.
- The `ifeq ($(ls -al $(which cc) ...), "clang")` block that is supposed to add `-Wno-type-limits -Wno-maybe-uninitialized` **does not work** — that is shell syntax inside a make `ifeq`, which make never evaluates, so the condition is always false and those flags are never applied. The code compiles clean under `-Wall -Wextra -Werror` without them, so this is currently harmless dead config.
- The `autoindex` target is not `.PHONY`; after the first build the `./autoindex` file exists, so the recipe (and thus the aindex sub-make + `cp`) may be skipped on subsequent `make` runs even if `aindex/` sources changed. Use `make re` or `make -C aindex` to force a rebuild of the helper.

### Test configs and assets

Config files under `conf/` use a `__WWWROOT__` placeholder. `make prepare-confs` substitutes the absolute `www` path and writes runnable copies into `conf/generated/`. Web assets live under `www/`. There is a Clojure test harness wired via `make test` (installs the Clojure CLI to `~/.local`, runs `webserv-tests`).

```bash
make prepare-confs
./webserv conf/generated/base.conf
```

## Constraints

- **C++98 only** — no C++11 or later. No lambdas, no `auto`, no range-for, no `nullptr`.
- **`-Wall -Werror -Wextra`** — all warnings are errors.
- **Linux only** — uses `epoll`, not `poll` or `select`.

## Source file status

The old `.new`-file migration is **finished and merged**. The live implementation lives in the plain `src/*.cpp` files (`ClientConnection.cpp`, `Request.cpp`, `Response.cpp`, `ScratchBuffer.cpp`, …); there are no `.new` files anymore.

The Makefile (`SRCS`) compiles:
`webserv.cpp`, `ConfigParser.cpp`, `Config.cpp`, `ServerConnection.cpp`, `ClientConnection.cpp`, `EpollLoop.cpp`, `ServerBlock.cpp`, `Request.cpp`, `Response.cpp`, `ScratchBuffer.cpp`, `utils.cpp`.

**Not compiled (dead code — do not edit, prefer deleting):**
- `src/Connection.cpp` — contains a *second, conflicting* inline definition of `class ClientConnection` (different member layout, old state enum). Not in the Makefile, so no ODR clash today, but it is misleading and a footgun if ever added to the build.
- `src/CgiConnection.cpp` — the old standalone CGI connection class; CGI is now driven entirely from inside `ClientConnection`.
- `src/ServerBlock.cpp` — a 3-line legacy stub (compiled but effectively empty).
- Stale headers: `inc/CgiConnection.hpp`, and the unused bits of `inc/Connection.hpp`.

## Architecture

### Event Loop

`EpollLoop` is a singleton (`get_instance()`) that owns all active connections as `map<int, Connection*>` (fd → Connection). `run()` calls `epoll_wait()` with an **infinite timeout (`-1`)**, dispatches each ready event to `conn->handle(events)`, then flushes the `_deletion_queue` at the end of the tick (connections are queued for deletion during event handling rather than freed immediately, to avoid dangling pointers within the same batch).

SIGINT sets a `sig_int` flag that stops the loop cleanly. `main()` installs `SIG_IGN` for SIGPIPE so writes to broken CGI/client pipes don't kill the server.

- `add(conn)` — register `conn->fd` with `EPOLLIN | EPOLLERR | EPOLLHUP`
- `mod(conn, events)` — replace the watched event mask on the *same* fd
- `rearm(conn, events, new_fd)` — **EPOLL_CTL_DEL the old fd, swap `conn->fd` to `new_fd`, and re-ADD with a new mask.** This is how a single `ClientConnection` migrates between watching the client socket, the CGI stdin pipe, and the CGI stdout pipe over its lifetime.
- `del(conn)` — enqueue for deletion; deregistered, closed, and freed in `clear()` at end of tick

> **Known gaps in the loop:** `epoll_wait` `EINTR` is not handled (commented-out `continue`). An event later in the same batch can dereference a `Connection` already `del()`'d earlier in the batch — the loop does not re-check `_connections.count(fd)` before dispatching. Both are "must not crash" risks flagged in-source.

### Connection Hierarchy

```
Connection (abstract; owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
└── ClientConnection  — the whole request→response lifecycle (parsing, CGI, autoindex, response)
```

`ServerConnection::handle()` does a **single** `accept()` per readiness event (the multi-accept loop is commented out; correct under level-triggered epoll, slightly less efficient under bursts). Note: leftover merge-conflict marker comments (`<<<<<<< HEAD` / `>>>>>>>`) are still present in `ServerConnection.cpp`.

### ClientConnection — the state machine

`ClientConnection` (`ClientConnection.{hpp,cpp}`) is the heart of the server. It holds a `ScratchBuffer _buf`, a `Request _req`, a `Response _res`, an `std::fstream _stream` (for the file/CGI-temp body), CGI fds/pid, and a `_state`:

```
REQ_LINE → REQ_HEADERS → REQ_SETUP → ┐
                                       ├─→ RESPONSE                    (static GET / errors / redirects)
                                       ├─→ DISCARD_BODY → RESPONSE     (413: drain the oversized body, then respond)
                                       └─→ REQ_BODY → CGI_TRANSMIT_BODY → CGI_HEADERS → CGI_BODY → RESPONSE  (CGI *and* autoindex)
```

- `DISCARD_BODY` is entered when `content_length` exceeds `client_max_body_size` (413): the body is read and thrown away so the connection can be reused/closed cleanly before the error response is sent.
- **Autoindex reuses the CGI output states.** `setup_autoindex()` forks the external `./autoindex` helper and drops into `CGI_HEADERS`/`CGI_BODY`, exactly like a CGI script. The `AUTOI_HEADERS`/`AUTOI_BODY` enum values still exist but are **unused/dead**.

`handle(events)` dispatches by state:
- `REQ_BODY` / `CGI_TRANSMIT_BODY` → `handle_cgi_input()` (stream client body → CGI stdin)
- `CGI_HEADERS` / `CGI_BODY` → `handle_cgi_output()` (CGI stdout → temp file)
- `DISCARD_BODY` → read-and-drop until `content_length` reached, then `setup_res()`
- Otherwise, on `EPOLLIN`: fill `_buf` from the socket, then run `handle_req_line()` → `handle_req_headers()` → `handle_setup()` as the buffer accumulates.
- On `EPOLLOUT` in `RESPONSE`: `handle_response()` drains buffered headers + file to the socket; returns false when done → `epoll.del(this)`.

Key steps:
1. **Request line** (`handle_req_line`): validates method token / URI / version, percent-encoding, decodes and normalizes the path (`.`/`..` collapsing via `normalize_req_path`); sets `_req.status` (400/501/505/414 on failure). Bare `METHOD` with no URI is only accepted for POST (treated as HTTP/0.9).
2. **Headers** (`handle_req_headers` + `parse_req_headers`): lowercases keys, extracts `Host`→`hostname`/`port`, `Content-Length`; 411 if POST has no length; 431 if the header block overflows the buffer.
3. **Setup** (`handle_setup`): resolves the virtual server via `Host`, then walks the `Location` tree in a loop bounded by `REDIRECT_LIMIT` (default 5), applying `return` directives, `index`, `autoindex`, method-allow, body-size, and file-existence checks. Error pages are resolved via `epi_redirect()` (internal redirect to the configured `error_page`, or a body-less response).
4. **CGI** (`setup_cgi`): `pipe()`×2 + `fork()` + `execve(interpreter, [interpreter, script], envp)`. Env includes `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `REQUEST_METHOD`, `SCRIPT_FILENAME`, `PATH_INFO`, `QUERY_STRING`, `SERVER_NAME`, plus any `cgi_param` pairs. Pipe ends are made non-blocking and the connection `rearm()`s onto them. POST bodies are streamed from the client buffer to CGI stdin; CGI stdout is buffered to a temp file (`/tmp/cgi_XXXXXX`), then the headers (incl. a `Status:` override) are split from the body and `finalize_cgi()` `waitpid()`s and builds the response.
5. **Static response** (`setup_res`): builds status line + headers (`Location`/`Allow`, `Date`, `Content-Length` from `stat`), opens the file via `_stream`, `mod()`s to `EPOLLOUT`. `setup_internal_error()` swaps in a static 500.

### ScratchBuffer

`ScratchBuffer` is a fixed-capacity byte buffer with two cursors: `readpos` (produce/write-in position) and `writepos` (consume position). `fill(...)` appends data **into** the buffer (from an fd, an `fstream`, or a literal); `feed(...)` drains data **out** of the buffer (to an fd or `fstream`). `fill_capacity()` is space remaining for `fill`; `feed_capacity()` is bytes pending to `feed`. `find(char/string)` searches the produced region; `erase(from,to)` memmoves to drop a span; `clear()` resets both cursors. `set_data()` lets it *reference* an external buffer (used for the static 500 string) instead of owning a heap buffer (`_ref_data`).

> Capacity comes from the matched server's `client_header_buffer_size` (default 1024). A request line / header block larger than this buffer yields 414/431 rather than growing.

> **Binary-unsafe (major bug):** the buffer treats its contents as a NUL-terminated C string throughout — `fill(fstream)` uses `fstream.get(..., '\0')`, `find()` uses `strnstr`/`strnchr` that stop at `\0`, and each `fill` writes a terminating `data[readpos]='\0'`. Any file containing a NUL byte (images, PDFs, most binaries) is **truncated at the first NUL** when served. `fill_eof()`/`feed_eof()` also compare against `'0'` (the digit) instead of `'\0'`, though those appear unused.

### Request

`Request` (`Request.hpp`) fields: `method` (`HttpMethod` enum), `uri` (raw), `path` (normalized/decoded), `query`, `version`, `headers` (map, lowercased keys), `content_length`, `host`/`hostname`/`port`, `status` (the current HTTP status being built toward), and the booleans `internal` (path is internal vs. an external URL) and `no_file` (build a body-less response).

### Response

`Response` (`Response.hpp`) is a thin **header-list builder**, not a writer: it owns a `std::vector<std::string> headers` queue and a static reason-phrase map. Builder methods: `add_status_line(version, code)`, `add_header_field(name, value|size_t)`, `add_allowed(loc)` (emits an `Allow:` list from the location's `limit_except`), `add_date()`, `add_header_end()`. The actual socket writing is done by `ClientConnection` (`buffer_res_headers()` / `buffer_file()` → `_buf.feed(fd)`); the file body is streamed through `_stream` + `_buf`. The response version is hardcoded to `HTTP_VERSION_STR` (`"HTTP/1.0"`) regardless of the request version.

### Configuration

`inc/Config.hpp` (+ `src/Config.cpp`) defines the config object tree: `Http` → `Server` → `Location`, plus `Port` and the `config::*` structs (`listen`, `redirect`, `index`, `autoindex`, `cgi`, `errorpageinfo`, `limit`, `mime`, etc.). `inc/ConfigParser.hpp` (+ `src/ConfigParser.cpp`) provides the nginx-style tokenizer/grouper: `Lexer` → `Grouper` (with `MainDirective`/`BodyDirective`/`SimpleDirective`). `main()` does `Grouper grouper(path); grouper.group();` then `Http http(grouper.main.body_directives[0])`.

Config hierarchy: `http {}` → `server {}` → `location {}`, with outer-scope directives inherited. Virtual-host resolution: `Http::get_default_server(sockaddr_in)` picks the listen-address default; `Http::get_server(sockaddr_in, hostname)` refines by `server_name`. `Server::get_location(uri)` / `Location::get_location(uri)` resolve the route. Accessors used by `ClientConnection` include `get_root()`, `get_body().max_size`, `get_cgi()`, `get_redirect()`, `get_index()`, `get_autoindex()`, `get_errorpages()`, `get_limit()`, `get_header().buffer_size`.

Key directives:

| Directive | Scope | Notes |
|---|---|---|
| `listen <ip>:<port> [default_server] [backlog=N]` | server | multiple allowed |
| `server_name <name>...` | server | virtual host selection |
| `root <path>` | http/server/location | |
| `error_page <code>... <uri>` | http/server/location | optional response-code override; can redirect internally or to an external URL |
| `return <code> <url\|path>` | location | redirect (`redirect` struct) |
| `index <file>` | http/server/location | default file for a directory |
| `autoindex on\|off` | http/server/location | directory listing, generated by forking the `./autoindex` helper |
| `client_max_body_size <size>` | http/server/location | k/m suffixes; default 1 MiB |
| `client_header_buffer_size`, `large_client_header_buffers` | http/server | sizes the `ScratchBuffer` |
| `client_body_buffer_size`, `client_body_timeout` | http/server/location | **timeouts are parsed but not enforced at runtime** |
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
4. Setup decides: static `RESPONSE`, `DISCARD_BODY` (413), CGI, or autoindex (both fork + `rearm()` onto pipes).
5. CGI/autoindex: stream client body → child stdin; child stdout → temp file; parse headers; `waitpid`; `rearm()` back to the client fd in `RESPONSE`.
6. Write: `handle(EPOLLOUT)` in `RESPONSE` drains headers + file via `_buf.feed(fd)`; on completion → `epoll.del(this)`.
7. Error/hangup at any stage: `epoll.del(conn)` → closed and freed at end of tick.

## Source Layout

- `src/` — the compiled set listed under **Source file status** above. `Connection.cpp`, `CgiConnection.cpp` are dead/uncompiled; `ServerBlock.cpp` is a stub.
- `inc/` — headers (note the dead `CgiConnection.hpp`, stale bits of `Connection.hpp`), plus `Logger.hpp`, `Config.hpp`, `ConfigParser.hpp`.
- `aindex/` — the standalone `./autoindex` helper (its own Makefile; `main(argc, argv)` takes the URL path and the filesystem path of the directory to list).
- `conf/` — example configs (use `__WWWROOT__`); `conf/generated/` holds `make prepare-confs` output.
- `www/` — static assets, error pages (`404.html`, `50x.html`), `cgi-bin/`.
- `webserv-tests/` — Clojure integration test suite (`make test`). Regression tests for the newly found bugs live in `resilience_test.clj` (half-close busy-loop, `%25` decode loop, LF-only CGI headers, plus the second-pass additions: oversized-body/half-close spin, `%ff` high-bit escape survival, client-closes-mid-response survival — each `:each`, timeout-guarded via `server/raw-request-timeout` / `responsive?`), `cgi_robustness_test.clj` (blocking `waitpid`, `EPOLLHUP` body truncation, no-output spin, oversized CGI header, lowercase `status:`, stray leading CRLF — `:each`, base.conf), `static_index_test.clj` (`index` appends to the directory vs. replacing the path — `:once`, `subdir_index.conf`), `parse_error_test.clj` (also: `Content-Length` trailing junk, bogus `HTTP/1.10`), `redirect_test.clj` (internal vs. external `return`, `client_header_buffer_size`), and `content_type_test.clj` (missing `Content-Type`, CGI-without-`Status`). They rely on `conf/redirect_buffer.conf` / `conf/subdir_index.conf`, the CGI helpers `www/cgi-bin/{no_status,lf_headers,slow_exit,big_body,no_output,big_header,lc_status,fixed_body,env}.py`, and the assets `www/idxdir/index.html` / `www/big.txt`.
- `incremental_versions/` — historical snapshots, not compiled.

## Subject scope & known gaps

The assignment (`subject.txt`) is the 42 *webserv*: a non-blocking C++98 HTTP server driven by a single `epoll`, never reading/writing a socket/pipe without prior readiness, never checking `errno` after read/write, must not crash, GET/POST/DELETE, file upload, configurable error pages / body size / routes / redirects / directory listing / CGI by extension, multiple listen ports.

What is solid: single-`epoll` I/O; no `errno` inspected after read/write (subject-compliant); SIGPIPE ignored; non-blocking sockets; config parser + virtual-host resolution; internal-redirect / error-page loop with a redirect limit; the CGI pipe/`rearm` state machine; `is_dir()` (now correctly uses `st_mode & S_IFMT == S_IFDIR`); directory listing (now generated, no longer a stub).

Bugs/gaps worth knowing before extending (verified against the current code and at runtime):

**Missing subject requirements**
- **DELETE doesn't delete.** No `unlink`/`remove`; DELETE is routed like GET and serves the file (returns 200, file remains).
- **File upload (non-CGI POST) is unimplemented.** `is_method_allowed()` rejects POST unless the location has `cgi_pass`; POST bodies are only ever streamed to a CGI process, never written to an upload location on disk.
- **Chunked transfer-encoding is not handled** — only `Content-Length` bodies are read; `Transfer-Encoding: chunked` is not un-chunked.
- **No request/idle timeout enforcement.** `epoll_wait` blocks with timeout `-1` and the parsed `client_*_timeout` values are never applied, so a slow/idle client can hold a connection open indefinitely.
- **No compliant README.md** in the subject-required format (italic first line, Description / Instructions / Resources incl. AI-usage). The existing `README.md` is internal architecture notes.

**Correctness bugs**
- **Binary files are corrupted** (NUL truncation in `ScratchBuffer`, see above) — static serving works only for text-like files.
- **Parse-error status is clobbered.** `handle_req_line`/`handle_req_headers` set e.g. 400, but `handle_setup()` unconditionally resets `_req.status` to 200/201 and routes normally. `GET\r\n\r\n` returns 405 (not 400).
- **`handle_req_headers` colon check** is `if (!colon)` — only true when the colon is at index 0, so a header line with **no** colon is accepted rather than rejected (400).
- **`handle_cgi_output` hangup check** is `events & (EPOLLHUP | EPOLLHUP)` — a duplicated flag; it should be `EPOLLHUP | EPOLLERR`, so an error-close on the CGI pipe is not detected.
- **CGI env is incomplete** — `CONTENT_LENGTH` and `CONTENT_TYPE` are never set (only mentioned in a comment), so a CGI can't reliably read a POST body's length.
- **`fork()` is used for autoindex** — the subject restricts `fork` to CGI only; the external `./autoindex` helper is forked for directory listing, which is likely to be flagged at evaluation. Directory listing should be generated in-process (`opendir`/`readdir`).
- **CGI temp files leak** — `/tmp/cgi_XXXXXX` files are created but never `unlink`'d.
- **Duplicate request headers** are silently dropped (`FIXME` in source).
- **EpollLoop use-after-del / no `EINTR` handling** (see Event Loop above) — "must not crash" risks.

**Newly found (2026-07, verified at runtime — not previously documented)**

These are covered by regression tests under `webserv-tests/` (see the test list below); each is KNOWN-FAILING today.

- **Half-closed / incomplete request → 100 % CPU busy-loop (grade-0 resilience risk).** A client that sends a partial request and then `shutdown(SHUT_WR)` (or just drops) pegs a CPU core indefinitely. When `read()` returns 0 (peer closed at EOF), `ScratchBuffer::fill()` returns 0, the state stays `REQ_LINE`/`REQ_HEADERS`, and the socket remains EPOLLIN-ready (EOF is "readable"), so `epoll_wait` re-delivers the fd every iteration forever. `ClientConnection::handle()` never checks for `read()==0` to `del()` the connection. Idle server = 0 % CPU; one half-close = ~80–95 %. A handful of such clients is a trivial DoS. (`ClientConnection.cpp:149-166`, `ScratchBuffer.cpp:64`.)
- **`decode_http()` infinite loop on any URI that decodes to a literal `%` (grade-0 hang).** `GET /%25` hangs forever and spins the CPU: `%25` decodes to `%`, the `while (find('%'))` loop then finds the bare `%`, `is_valid_hex` fails, nothing is replaced, and the scan position is never advanced → infinite loop. Reachable by any client (`/%25`, `/%2525`, …). (`ClientConnection.cpp:275`.)
- **Internal `return <code> <path>` always yields 500.** `location /old { return 301 /index.html; }` returns 500, not a redirect. In `handle_setup` the `return` branch sets `_req.path` but **never re-resolves `_loc`**, so the same location's `return` fires on every loop iteration until `REDIRECT_LIMIT`, which forces a 500. Only external returns (`return 302 https://…`) work, because they set `internal=false` and break the loop. Internal redirects — a required config feature — never work. (`ClientConnection.cpp:529-544`.)
- **`client_header_buffer_size` / `large_client_header_buffers` are dead — `ScratchBuffer::set_capacity()` never updates `capacity`.** `set_capacity()` allocates a new buffer but never assigns `capacity = cap`, so `fill_capacity()` keeps using the old 1024 default; the directive has no effect (a 3 KB request line under a 16 KB configured buffer still collapses into a 500). The same function's `std::min(sizeof(data), sizeof(new_data))` is `min(8,8)=8` — it copies 8 bytes of a pointer, not the buffer contents (harmless only because it is called pre-fill). (`ScratchBuffer.cpp:38-52`.)
- **No `Content-Type` on any response; `types` / `default_type` are parsed-but-unused.** `setup_res` emits `Allow`, `Date`, `Content-Length` but never `Content-Type`, and no `content-type`/`mime` lookup exists in `ClientConnection.cpp` or `Response.cpp`. Browsers MIME-sniff HTML but mishandle CSS/JS/images, so "serve a fully static website" is only partially met.
- **CGI header parsing is CRLF-only — LF-terminated CGI headers hang the connection.** `handle_cgi_output` splits headers on `"\r\n"` exclusively; a CGI that emits Unix `\n` line endings (RFC 3875 permits LF) never terminates header parsing and the request times out. (`ClientConnection.cpp:760`.)
- **A CGI that omits `Status:` produces a response with no status line.** When the CGI sends no `Status:` header, the server never calls `add_status_line`, so the response begins with the CGI's own headers and has no `HTTP/x 200` line at all (should default to 200). (`ClientConnection.cpp:758-799`.)
- **`Allow` header is emitted on success responses, including redirects.** `setup_res` calls `add_allowed()` for every internal response, so a plain `200 OK` and even a `301`/`302` carry `Allow: GET, POST, DELETE`. `Allow` belongs on `405`. (`ClientConnection.cpp:639`.)

**Additional issues found by source review (2026-07, second pass — not previously documented)**

These came out of a static read of the live sources (`ClientConnection.cpp`, `ScratchBuffer.cpp`, `EpollLoop.cpp`, `ServerConnection.cpp`, `Response.cpp`, `Request.cpp`). Each is a distinct defect a hostile or merely unlucky client can reach against our own server; the point of listing them is to harden against them (or pin the correct behavior with a regression test). Where a test exists it is noted; some are code-review-only (no black-box surface) and are marked as such.

_Crash / hang class (grade-0 resilience risk):_

- **`ScratchBuffer::feed(int fd)` corrupts state on a failed `write`** (`ScratchBuffer.cpp:88-94`). `writeret` is `size_t`, so `write()` returning `-1` becomes `SIZE_MAX`; the `writeret > 0` guard passes and `writepos += SIZE_MAX` wraps. `feed_capacity()` (`readpos - writepos`) then underflows to a huge value and the next `feed` calls `write(fd, data + writepos, huge)` with an out-of-bounds pointer → likely crash. Trigger is ordinary: the client disconnects mid-response (`write` gets `EPIPE`, SIGPIPE is ignored) or the CGI closes stdin early. `fill(int fd)` (line 64) has the same signed→unsigned hazard; the `DISCARD_BODY` caller only survives by storing the result in an `int`. This is the most dangerous undocumented bug. _(Regression: resilience-test client-closes-mid-response responsiveness probe.)_
- **`setup_cgi()` failure leaves the connection hung forever** (`ClientConnection.cpp:608-611`). When `setup_cgi()` returns false (pipe/fork failure under fd or process pressure), `handle_setup` sets `_req.status = 500` and returns without calling `setup_res()`; the fd is never `mod()`'d to `EPOLLOUT`, so no response is built or sent and the client waits indefinitely. _(Code-review-only: reliably triggering fd exhaustion from the harness is fragile.)_
- **`finalize_cgi()` blocks the event loop in `waitpid(pid, NULL, 0)`** (`ClientConnection.cpp:888`). A CGI that closes stdout but keeps running (sleeps, daemonizes) stalls the entire single-threaded loop until the child exits; every other client hangs. Needs `WNOHANG` + reap-later or a `kill`. _(Regression: cgi-robustness slow-exit responsiveness probe.)_
- **CGI output truncated when `EPOLLHUP` arrives with data still buffered in the pipe** (`ClientConnection.cpp:755-810`). Each event does at most one `fill()` (≤ ~1023 B), then on `EPOLLHUP` immediately `finalize_cgi()`s — deregistering the pipe with up to 64 KB unread. Any CGI body larger than the scratch buffer can lose its tail. Fix: drain until `read` returns 0 before finalizing. _(Regression: cgi-robustness large-body length check.)_
- **A CGI that exits before emitting a complete header line pegs a core** (`ClientConnection.cpp:780-781`). In `CGI_HEADERS`, if no `"\r\n"` is present the function returns *before* the `EPOLLHUP` check; once the child exits, level-triggered `EPOLLHUP` re-fires every loop iteration and is never handled → 100 % CPU. Same family as the documented half-close spin, distinct code path (a CGI crashing before flushing is common). _(Regression: cgi-robustness no-output timeout probe.)_
- **Zombie CGI processes on client abort** (`ClientConnection.cpp:817-819`). If the client aborts mid-POST (`REQ_BODY` + `EPOLLERR/HUP` → `del(this)`), the forked child is never `kill`ed or `waitpid`ed; the destructor closes only pipe fds. Zombies accumulate for the server's lifetime. _(Code-review-only.)_
- **A single `set_nonblocking`/`accept` failure can take the server down or busy-spin** (`ServerConnection.cpp:20-27`, `utils.cpp:10-16`). `set_nonblocking()` throws on `fcntl` failure; nothing between `conn->handle()` and `main()`'s catch handles it, so the process exits. Separately, `accept()` returning `-1` on `EMFILE` just returns while the listen fd stays `EPOLLIN`-ready → busy-spin until an fd frees. _(Code-review-only.)_
- **`DISCARD_BODY` has its own half-close busy-loop and no size cap** (`ClientConnection.cpp:129-144`). If the client closes before delivering the full oversized body, `read()` returns 0 forever and `_written_body` never reaches `content_length` → EOF-readable spin (the documented half-close bug only covers `REQ_LINE`/`REQ_HEADERS`). Also the server will read as many bytes as the client's `Content-Length` claims (e.g. 10 GB) rather than closing. _(Regression: resilience-test oversized-body half-close probe.)_

_Correctness class:_

- **CGI body gets a stray leading CRLF** (`ClientConnection.cpp:780-798`). The header loop stops at the blank line (`pos == 0`) but never `erase`s those 2 bytes, so the `"\r\n"` separator is written into the body temp file; every CGI body starts with an extra CRLF. Substring-matching tests miss it. _(Regression: cgi-robustness exact-body-prefix check.)_
- **`index` replaces the whole path instead of appending to the directory** (`ClientConnection.cpp:564`). `_req.path = _loc->get_index().path` means `GET /subdir/` with `index index.html` serves `/index.html` from the root, not `/subdir/index.html` — diverges from nginx and from the subject's "default file to serve when the requested resource is a directory". _(Regression: static-index subdir test.)_
- **Off-by-one deadlock window at `fill_capacity() == 1`** (`ClientConnection.cpp:150,376,420`; `ScratchBuffer.cpp:103`). The read guard fires only when `fill_capacity() > 1`, but the 414/431 error guards fire only when `fill_capacity() <= 0`; at exactly 1 neither happens → no read, no error, socket stays readable → spin. _(Code-review-only: hitting the exact boundary from the harness is fragile.)_
- **A CGI header line longer than the buffer wedges the response** (`ClientConnection.cpp:657-662`). `buffer_res_headers()` requires `fill_capacity() > header.size()`; a >1 KB CGI header (large `Set-Cookie`, etc.) can never be placed, then `handle_response` writes 0 bytes and returns false → connection closed mid-response. _(Regression: cgi-robustness big-header probe.)_
- **Weak value validation.** CGI `Status` is matched case-sensitively (`ClientConnection.cpp:770`), so a compliant lowercase `status:` is treated as an ordinary header and hits the documented no-status-line bug; `Content-Length` parsing (`452-455`) accepts trailing junk (`123abc`) and negatives wrap through `size_t`; the version check `find_first_not_of('0', 8)` (line 352) accepts `HTTP/1.10`/`HTTP/1.100`. _(Regressions: parse-error Content-Length-junk and version-1.10 tests; cgi-robustness lowercase-status test.)_
- **Latent UB in URI decoding.** `decode_hex` indexes `hex_val[256]` with a signed `char` cast (`ClientConnection.cpp:272`) — a high-bit byte gives a negative index; `decode_http` reads `new_uri[pos+1]`/`[pos+2]` and `operator[](size())` without bounds checks (line 282), reachable in the same `%`-at-end scenario as the documented decode loop. _(Regression: resilience-test `/%ff` responsiveness probe.)_
- **Uninitialized members.** `Request::port` is missing from the ctor init list (`Request.cpp:8-10`); `_cgi_pid` and `_written_body` are not initialized in the `ClientConnection` ctor — `waitpid(_cgi_pid, …)` on a garbage pid could reap an unrelated child. _(Code-review-only.)_
- **Destructor / fd bookkeeping.** A connection dying while `fd` is a CGI pipe never closes `_client_fd` (socket leak), and `delete_conn` + `~ClientConnection` can double-close `_cgi_stdout_fd`. _(Code-review-only.)_

_Compliance / housekeeping:_

- **`mkstemp`** (`ClientConnection.cpp:791`) is not on the subject's allowed-functions list — an evaluation flag, same category as fork-for-autoindex. `bzero`/`strncpy` are POSIX-isms; prefer `memset` / C++ forms given the "prefer C++ versions" rule.
- Cosmetic: `"HTTP Version Not SUpported"` typo (`Response.cpp:39`); `parse_cgi_headers()` is now dead code; `conf/redirect_buffer.conf` is untracked in git although the redirect tests depend on it, so a fresh clone fails `make test`.

Priority order for fixes: the half-close busy-loop and the `%25` decode loop first — both are remotely triggerable, both violate "must remain operational / never hang indefinitely", and either alone is an evaluation-failing DoS. Then the `ScratchBuffer::feed` `size_t` wrap (crash on a client that closes mid-response) and the CGI hang paths (`setup_cgi` failure, blocking `waitpid`, no-output spin), then the CGI data-integrity bugs (HUP truncation, stray leading CRLF).
