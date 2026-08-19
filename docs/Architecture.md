## Build Commands

```bash
make          # Build ./webserv (release, -O3)
make DEBUG=1  # Build with -g3 (no -O3)
make re       # Full rebuild
make clean    # Remove obj/
make fclean   # Remove obj/ and webserv
make test     # Install the Clojure CLI if needed, generate confs, run webserv-tests
```

`make` builds a **single** binary, `./webserv`. Directory listing is compiled into it (`src/autoindex.cpp` + `src/autoindex_File.cpp`); the old `aindex/` sub-project and the separate `./autoindex` helper binary are superseded — but both are still **tracked in git** at the repo root, so removing them needs `git rm -r`. There is **no parser submodule** either: config parsing is compiled directly from `src/ConfigParser.cpp` and `src/Config.cpp`.

Run the server:

```bash
./webserv [configuration file]   # defaults to webserv.conf if no arg
```

> ⚠️ **The no-argument form does not work today.** There is no `webserv.conf` at the repo root, and a config path that cannot be opened makes the lexer spin forever at 100 % CPU instead of erroring out. An empty or malformed config segfaults or aborts. Always pass a `conf/generated/*.conf` path until `Gaps_and_Issues.md` issue 25 is fixed.

### Makefile notes

- `$(ODIR)` (`obj/`) is a normal prerequisite of `$(NAME)` (listed before `$(OBJS)`), not an order-only one. `make fclean && make -j8` succeeds today, but the race is latent — the correct form is `$(ODIR)%.o: %.cpp | $(ODIR)`.
- The `ifeq ($(ls -al $(which cc) ...), "clang")` block that is supposed to add `-Wno-type-limits -Wno-maybe-uninitialized` **does not work** — that is shell syntax inside a make `ifeq`, which make never evaluates, so the condition is always false and those flags are never applied. The code compiles clean under `-Wall -Wextra -Werror` without them, so this is currently harmless dead config.

### Test configs and assets

Config files under `conf/` use a `__WWWROOT__` placeholder. `make prepare-confs` substitutes the absolute `www` path and writes runnable copies into `conf/generated/` (gitignored). Web assets live under `www/`. There is a Clojure test harness wired via `make test` (installs the Clojure CLI to `~/.local`, runs `webserv-tests`). `conf/`, `www/` and `webserv-tests/` are all tracked in git, so a fresh clone can run the suite.

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
`webserv.cpp`, `ConfigParser.cpp`, `Config.cpp`, `ServerConnection.cpp`, `ClientConnection.cpp`, `EpollLoop.cpp`, `ServerBlock.cpp`, `Request.cpp`, `Response.cpp`, `ScratchBuffer.cpp`, `utils.cpp`, `autoindex.cpp`, `autoindex_File.cpp`.

**Dead code — do not edit, prefer deleting:**
- `src/Connection.cpp` — not compiled. Contains a *second, conflicting* inline definition of `class ClientConnection` (different member layout, old state enum). Not in the Makefile, so no ODR clash today, but it is misleading and a footgun if ever added to the build.
- `src/CgiConnection.cpp` — not compiled. The old standalone CGI connection class; CGI is now driven entirely from inside `ClientConnection`.
- `src/ServerBlock.cpp` — a 3-line legacy stub that *is* in `SRCS` and gets linked into the binary; nothing uses it (`webserv.cpp`'s include is commented out).
- `inc/PidCollector.hpp` — an abandoned first sketch of CGI pid tracking, superseded by `EpollLoop::_children` before it was ever used. It does not even compile (no `<set>` include, missing semicolon after the class) and is included by nothing.
- Stale headers: `inc/CgiConnection.hpp`, and the unused bits of `inc/Connection.hpp`.
- `ClientConnection::parse_cgi_headers()` — declared, defined, never called; a stale duplicate of the header parsing that now lives inline in `handle_cgi_output`, still carrying the old case-sensitive `key == "Status"` bug.

## Architecture

### Event Loop

`EpollLoop` is a singleton (`get_instance()`) that owns all active connections as `map<int, Connection*>` (fd → Connection). One tick of `run()`:

1. `epoll_wait()` with a **5 ms timeout** (`EINTR` and a SIGINT-during-wait are tolerated: `ready` is clamped to 0 and the loop continues).
2. Dispatch each ready event to `conn->handle(events)`, **each call wrapped in try/catch** — an exception escaping a connection logs and is swallowed instead of unwinding out of `main()`.
3. **Sweep every connection for timeouts** (`cur_time - clicon->_last_update > clicon->_timeout` → `clicon->handle_timeout()`), same try/catch.
4. `clear()` — flush the `_deletion_queue` (connections are queued for deletion during event handling rather than freed immediately, to avoid dangling pointers within the same batch).
5. `reap_children()` — sweep the CGI child table (below).

`ClientConnection::handle_timeout()` splits by state: a connection still reading (`_state < DISCARD_BODY`) gets `_req.status = 408` and is pushed back through `handle_setup()` so a real `408 Request Timeout` response is built; anything past that (i.e. a client that has stopped *reading*) is simply `del()`'d. `_last_update` is refreshed by `update_timestamp()` on every successful read/write.

SIGINT sets a `sig_int` flag that stops the loop cleanly. `main()` installs `SIG_IGN` for SIGPIPE so writes to broken CGI/client pipes don't kill the server. The epoll fd is created with `EPOLL_CLOEXEC`; listen sockets use `SOCK_CLOEXEC`, accepted sockets go through `set_cloexec()`, and both CGI pipe pairs get `FD_CLOEXEC` — so a forked CGI inherits nothing but its own two pipe ends (see `past_issues/FD_CLOEXEC.md`).

- `add(conn)` — register `conn->fd` with `EPOLLIN | EPOLLERR | EPOLLHUP`
- `mod(conn, events)` — replace the watched event mask on the *same* fd
- `rearm(conn, events, new_fd)` — **EPOLL_CTL_DEL the old fd, swap `conn->fd` to `new_fd`, and re-ADD with a new mask.** This is how a single `ClientConnection` migrates between watching the client socket, the CGI stdin pipe, and the CGI stdout pipe over its lifetime.
- `del(conn)` — enqueue for deletion; deregistered, closed, and freed in `clear()` at end of tick

**CGI child table.** The loop — not `ClientConnection` — owns CGI process lifetime, so nothing ever blocks in `waitpid`:

- `track_child(pid, timeout)` — called right after `fork()`, records a deadline of `now + CGI_TIMEOUT` (10 s, `ClientConnection.hpp`)
- `kill_child(pid)` — sets the deadline to 0, i.e. "signal it on the next sweep". Called from `~ClientConnection` (client aborted) and from the 502 branch of `handle_cgi_output`
- `reap_children()` — once per tick: `waitpid(pid, NULL, WNOHANG)` on each entry; reaped or `ECHILD` → drop it. Past the deadline it sends SIGTERM, then SIGKILL `CGI_KILL_GRACE` (2 s) later if the child ignored it
- `~EpollLoop` SIGKILLs and blocking-reaps anything still tracked

> **Known gap in the loop:** an event later in the same batch can dereference a `Connection` already `del()`'d earlier in the batch — the loop does not re-check `_connections.count(fd)` before dispatching. A "must not crash" risk, flagged in-source, and one the try/catch does **not** cover (a dangling `this` is UB, not an exception). See `Gaps_and_Issues.md` issue 6. (`epoll_wait` `EINTR` was the other gap here; fixed 2026-08-05 — see `past_issues/EINTR_unhandled.md`.)

### Connection Hierarchy

```
Connection (abstract; owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
└── ClientConnection  — the whole request→response lifecycle (parsing, CGI, autoindex, response)
```

`ServerConnection::handle()` does a **single** `accept()` per readiness event (the multi-accept loop is commented out; correct under level-triggered epoll, slightly less efficient under bursts), then `set_nonblocking()` + `set_cloexec()` on the new fd. A failed `accept()` just returns — harmless for `EINTR` under level-triggered epoll, but it is what makes EMFILE a busy-spin (`Gaps_and_Issues.md` issue 10).

### ClientConnection — the state machine

`ClientConnection` (`ClientConnection.{hpp,cpp}`) is the heart of the server. It holds a `ScratchBuffer _buf`, a `Request _req`, a `Response _res`, an `std::fstream _stream` (for the file/CGI-temp body), CGI fds/pid, and a `_state`:

```
REQ_LINE → REQ_HEADERS → REQ_SETUP → ┐
                                       ├─→ RESPONSE                    (static GET / autoindex / errors / redirects)
                                       ├─→ DISCARD_BODY → RESPONSE     (413: drain the oversized body, then respond)
                                       ├─→ REQ_BODY → RESPONSE         (static POST: body → file on disk)
                                       └─→ REQ_BODY → CGI_TRANSMIT_BODY → CGI_HEADERS → CGI_BODY → RESPONSE  (CGI)
```

- `DISCARD_BODY` is entered when `content_length` exceeds `client_max_body_size` (413): the body is read and thrown away so the connection can be reused/closed cleanly before the error response is sent. The check is front-loaded in `handle_setup()` *before* any file is opened, so an oversized POST never creates one.
- **Static POST** (no `cgi_pass` on the location): `setup_post()` opens `root + path` with `out | app`, `handle_post_leftover()` flushes whatever body bytes already sat in `_buf`, and `handle_post()` continues in `REQ_BODY` until `content_length` is reached, then `setup_res()` answers `201 Created`. ⚠️ The `REQ_BODY` branch of `handle_post()` currently jumps straight to `RESPONSE` instead of feeding the buffer to the file, so bodies larger than one buffer are truncated — see `Gaps_and_Issues.md` issue 1.
- **Autoindex is generated in-process** (`autoindex::as_html()` over `opendir`/`readdir`), written to a `/tmp/autoindex_XXXXXX` file, and served from the ordinary `RESPONSE` state. It no longer forks a helper and no longer borrows the CGI states; the `AUTOI_*` enum values are gone. ⚠️ The temp file is never unlinked and the hand-built header block has no `Content-Type` (`Gaps_and_Issues.md` issue 4).
- A **CGI without a request body** (GET, or POST with `content_length == 0`) skips `REQ_BODY`/`CGI_TRANSMIT_BODY` entirely: `setup_cgi()` closes the stdin pipe immediately and `rearm()`s straight onto stdout in `CGI_HEADERS`.
- **POST bypasses the resolution loop.** `handle_setup()` is `if (method == POST) { size check; setup_post(); } else while (…resolution loop…)`, so `limit_except`, file existence, `return`, `index` and `autoindex` are only consulted for non-POST methods — a known regression (`Gaps_and_Issues.md` issue 2).

`handle(events)` dispatches by state:
- `REQ_BODY` / `CGI_TRANSMIT_BODY` → `handle_cgi_input()` if the location has `cgi_pass`, otherwise `handle_post()` (client body → file)
- `CGI_HEADERS` / `CGI_BODY` → `handle_cgi_output()` (CGI stdout → temp file)
- `DISCARD_BODY` → read-and-drop until `content_length` reached, then `setup_res()`
- Otherwise, on `EPOLLIN`: fill `_buf` from the socket, then run `handle_req_line()` → `handle_req_headers()` → `handle_setup()` as the buffer accumulates.
- On `EPOLLOUT` in `RESPONSE`: `handle_response()` drains buffered headers + file to the socket; returns false when done → `epoll.del(this)`.

Key steps:
1. **Request line** (`handle_req_line`): validates method token / URI / version, percent-encoding, decodes and normalizes the path (`.`/`..` collapsing via `normalize_req_path`); sets `_req.status` (400/501/505/414 on failure). Bare `METHOD` with no URI is only accepted for POST (treated as HTTP/0.9).
2. **Headers** (`handle_req_headers` + `parse_req_headers`): lowercases keys only, splits `Host` into `hostname` + a validated port substring (the port itself is discarded), reads `Content-Length`; 411 if POST has no length; 431 if the header block overflows the buffer. Duplicate header lines are silently dropped by `map::insert`.
3. **Setup** (`handle_setup`): resolves the virtual server via `Host`; a non-zero `_req.status` carried over from parsing is preserved and routed straight to `epi_redirect()` (this is what makes 400/501/505/414 come out accurate). Otherwise it walks the `Location` tree in a loop bounded by `REDIRECT_LIMIT` (default 5), applying `return` directives, `index`, `autoindex`, method-allow and file-existence checks — for non-POST methods; POST takes the separate branch described above. DELETE is executed here (`std::remove` → 204). Error pages are resolved via `epi_redirect()` (internal redirect to the configured `error_page`, or a body-less response). The function ends by resetting `_timeout` from `_loc->get_header().timeout` — which is always the 60 s default, since `config::header` is not inherited into `Location` (`Gaps_and_Issues.md` issue 8).
4. **CGI** (`setup_cgi`): `pipe()`×2 (both ends `FD_CLOEXEC`) + `fork()` + `execve(interpreter, [interpreter, script], envp)`, with the pid handed to `EpollLoop::track_child()`. Env is `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `REQUEST_METHOD`, `SCRIPT_FILENAME`, `PATH_INFO`, `QUERY_STRING`, `SERVER_NAME`, `CONTENT_LENGTH`/`CONTENT_TYPE` on POST, plus any `cgi_param` pairs — and **nothing else**: no `HTTP_*`, no `SERVER_PORT`/`SCRIPT_NAME`/`REMOTE_ADDR`, and no `chdir` into the script's directory (`Gaps_and_Issues.md` issues 27 and 28, both subject requirements). Parent-side pipe ends are made non-blocking and the connection `rearm()`s onto them. POST bodies are streamed from the client buffer to CGI stdin; CGI stdout is buffered to a temp file (`/tmp/cgi_XXXXXX`), then the headers (incl. a case-insensitive `Status:` override) are split from the body. `finalize_cgi()` reopens the temp file for reading, unlinks it, and `rearm()`s back to the client fd — it **does not** `waitpid`; reaping is the loop's job.
5. **Static response** (`setup_res`): builds status line + headers (`Location`/`Allow`, `Date`, `Content-Length` from `stat`, and `Content-Type` from the location's `types`/`default_type` map keyed on the path's extension), opens the file via `_stream`, `mod()`s to `EPOLLOUT`. `setup_internal_error()` swaps in a static 500. Two known warts: `Allow` is emitted on *every* internal response rather than only 405s (issue 19), and the reason-phrase map has no entry for 411/414/431/504, so those go out as e.g. `414 Unknown Status Code` (issue 29).

### ScratchBuffer

`ScratchBuffer` is a fixed-capacity byte buffer with two cursors: `readpos` (produce/write-in position) and `writepos` (consume position). `fill(...)` appends data **into** the buffer (from an fd, an `fstream`, or a literal); `feed(...)` drains data **out** of the buffer (to an fd or `fstream`). `fill_capacity()` is space remaining for `fill`; `feed_capacity()` is bytes pending to `feed`. `find(char/string)` searches the produced region; `erase(from,to)` memmoves to drop a span; `clear()` resets both cursors. `set_data()` lets it *reference* an external buffer (used for the static 500 string) instead of owning a heap buffer (`_ref_data`).

> Capacity comes from the matched server's `client_header_buffer_size` (default 1024). A request line / header block larger than this buffer yields 414/431 rather than growing.

> **Binary safety (fixed for the data path):** `fill(fstream)` now uses `fstream.read()`/`gcount()` (count-based), so served files — including the CGI temp-file body — pass through byte-clean; `feed(...)` was always count-based. The eof/fail semantics matter: `read()` sets eofbit+failbit on the final short read *after* `gcount()` bytes are delivered, which is what terminates `buffer_file` and satisfies `is_res_finished()`. Still string-oriented: `find()` uses `strnstr`/`strnchr` that stop at `\0` (only affects header *scanning*, and headers are text), the other `fill` overloads write a terminating `data[readpos]='\0'` (which is why `fill_capacity()` reserves one byte — keep the `-1`), and `fill_eof()`/`feed_eof()` compare against `'0'` (the digit) but appear unused.

### Request

`Request` (`Request.hpp`) fields: `method` (`HttpMethod` enum), `uri` (raw), `path` (normalized/decoded), `query`, `version`, `headers` (map — **keys** lowercased, values left case-intact), `content_length`, `host`/`hostname`, `status` (the current HTTP status being built toward), and the booleans `internal` (path is internal vs. an external URL) and `no_file` (build a body-less response). There is no `port` member any more — nothing read it, so it was deleted rather than wired into the CGI env.

> Since values stopped being lowercased, every consumer that compares one must do so case-insensitively itself (`equals_icase` lives in `utils.hpp`). `config::starts_with_scheme` and the CGI `Status:` check do; the **virtual-host lookup does not** — `_req.hostname` reaches `Port::get_server_by_name`'s plain `std::map` unfolded, so `Host: EXAMPLE.com` no longer matches `server_name example.com` (`Gaps_and_Issues.md` issue 26).

### Response

`Response` (`Response.hpp`) is a thin **header-list builder**, not a writer: it owns a `std::vector<std::string> headers` queue and a static reason-phrase map. Builder methods: `add_status_line(version, code)`, `add_header_field(name, value|size_t)`, `add_allowed(loc)` (emits an `Allow:` list from the location's `limit_except`), `add_date()`, `add_header_end()`. The actual socket writing is done by `ClientConnection` (`buffer_res_headers()` / `buffer_file()` → `_buf.feed(fd)`); the file body is streamed through `_stream` + `_buf`. The response version is hardcoded to `HTTP_VERSION_STR` (`"HTTP/1.0"`) regardless of the request version.

### Configuration

`inc/Config.hpp` (+ `src/Config.cpp`) defines the config object tree: `Http` → `Server` → `Location`, plus `Port` and the `config::*` structs (`listen`, `redirect`, `index`, `autoindex`, `cgi`, `errorpageinfo`, `limit`, `mime`, etc.). `inc/ConfigParser.hpp` (+ `src/ConfigParser.cpp`) provides the nginx-style tokenizer/grouper: `Lexer` → `Grouper` (with `MainDirective`/`BodyDirective`/`SimpleDirective`). `main()` does `Grouper grouper(path); grouper.group();` then `Http http(grouper.main.body_directives[0])`.

Config hierarchy: `http {}` → `server {}` → `location {}`, with outer-scope directives inherited. Inheritance is **explicit, per-struct copying**, not a fallback chain: `Location::from_server()` copies `root`, `body`, `output`, `mime`, `errorpages`, `index`, `autoindex` — and notably **not** `header`, so `client_header_timeout` / `client_header_buffer_size` never reach a `Location` (`Gaps_and_Issues.md` issue 8). `Location::operator=` — which the copy ctor delegates to, and which `copy_deep_container` therefore uses for every nested location — omits `header`, `output` **and `cgi`**, so a deep-copied location silently loses its `cgi_pass` (issue 31). Anything added to the structs needs adding to both places. Virtual-host resolution: `Http::get_default_server(sockaddr_in)` picks the listen-address default; `Http::get_server(sockaddr_in, hostname)` refines by `server_name`. `Server::get_location(uri)` / `Location::get_location(uri)` resolve the route. Accessors used by `ClientConnection` include `get_root()`, `get_body().max_size`, `get_cgi()`, `get_redirect()`, `get_index()`, `get_autoindex()`, `get_errorpages()`, `get_limit()`, `get_header().buffer_size`.

Key directives:

| Directive | Scope | Notes |
|---|---|---|
| `listen <ip>:<port> [default_server] [backlog=N]` | server | multiple allowed |
| `server_name <name>...` | server | virtual host selection |
| `root <path>` | http/server/location | |
| `error_page <code>... <uri>` | http/server/location | optional response-code override; can redirect internally or to an external URL |
| `return <code> <url\|path>` | location | redirect (`redirect` struct) |
| `index <file>` | http/server/location | default file for a directory; appended to the directory path |
| `autoindex true\|false` | http/server/location | directory listing, generated in-process; **not** nginx's `on`/`off` — `on` is a config error |
| `client_max_body_size <size>` | http/server/location | k/m suffixes; default 1 MiB; enforced (413) for CGI *and* static POST |
| `client_header_buffer_size`, `large_client_header_buffers` | http/server | *meant* to size the `ScratchBuffer`; currently dead (`set_capacity` never assigns `capacity`) |
| `client_header_timeout` | http/server | drives the 408 path — but only until `handle_setup()` overwrites `_timeout` with the location default |
| `client_body_buffer_size`, `client_body_timeout` | http/server/location | **`client_body_timeout` is parsed but never read at runtime** |
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
5. CGI: stream client body → child stdin; child stdout → temp file; parse headers; unlink the temp file; `rearm()` back to the client fd in `RESPONSE`. The child is reaped independently by `EpollLoop::reap_children()`. Autoindex: render in-process → temp file → `RESPONSE`. Static POST: client body → target file → `RESPONSE` (201).
6. Write: `handle(EPOLLOUT)` in `RESPONSE` drains headers + file via `_buf.feed(fd)`; on completion → `epoll.del(this)`.
7. Error/hangup at any stage: `epoll.del(conn)` → closed and freed at end of tick.

## Source Layout

- `src/` — the compiled set listed under **Source file status** above. `Connection.cpp`, `CgiConnection.cpp` are dead/uncompiled; `ServerBlock.cpp` is a stub.
- `inc/` — headers (note the dead `CgiConnection.hpp`, stale bits of `Connection.hpp`), plus `Logger.hpp`, `Config.hpp`, `ConfigParser.hpp`, `autoindex.hpp`.
- `conf/` — example configs (use `__WWWROOT__`); `conf/generated/` holds `make prepare-confs` output.
- `www/` — static assets, error pages (`404.html`, `50x.html`), `cgi-bin/`.
- `webserv-tests/` — Clojure integration test suite (`make test`). Regression tests for the bugs found in the review passes live in `resilience_test.clj` (half-close busy-loop, `%25` decode loop, LF-only CGI headers, oversized-body/half-close spin, `%ff` high-bit escape survival, client-closes-mid-response survival — each `:each`, timeout-guarded via `server/raw-request-timeout` / `responsive?`), `cgi_robustness_test.clj` (slow-exiting CGI vs. the loop, `EPOLLHUP` body truncation, no-output spin, oversized CGI header, lowercase `status:`, stray leading CRLF — `:each`, base.conf), `static_index_test.clj` (`index` appends to the directory vs. replacing the path — `:once`, `subdir_index.conf`), `parse_error_test.clj` (also: `Content-Length` trailing junk, bogus `HTTP/1.10`), `redirect_test.clj` (internal vs. external `return`, `client_header_buffer_size`), and `content_type_test.clj` (missing `Content-Type`, CGI-without-`Status`). They rely on `conf/redirect_buffer.conf` / `conf/subdir_index.conf`, the CGI helpers `www/cgi-bin/{no_status,lf_headers,slow_exit,big_body,no_output,big_header,lc_status,fixed_body,env,multi_header}.py`, and the assets `www/idxdir/index.html` / `www/big.txt` — all tracked in git now. `cgi_test.clj` additionally pins multi-header CGI responses (`test-cgi-multi-header-response-intact`), and `method_test.clj`'s DELETE test creates its own scratch file (never point DELETE tests at shared assets — DELETE works now).
  Three namespaces were added in the 2026-07-29 pass, each pairing a passing *control* with the failing defect so a red test means the bug and not a missing feature: `upload_test.clj` (single-buffer POST works / a 5 KB POST is truncated — `upload.conf`), `autoindex_test.clj` (the listing renders / it leaks a `/tmp/autoindex_*` per request and has no `Content-Type` — `autoindex.conf`), and `timeout_test.clj` (a stalled header block gets its 408 / a stalled *body* is held for the 60 s default — `timeout_body.conf`). The timeout tests use a local `send-and-idle` helper rather than `raw-request-timeout`, because half-closing the write side is detected on its own and would mask the timeout entirely.
  The 2026-08-05 pass added `cgi_lifecycle_test.clj` (6 tests, all passing) covering the CGI process lifetime rewrite: a well-behaved CGI is answered and reaped, a runaway is killed at `CGI_TIMEOUT`, a SIGTERM-immune CGI is escalated to SIGKILL, the loop stays responsive while one runs away, and aborted requests leave neither zombies nor leaked fds.
  One reading note: many older test descriptions still start with a literal `KNOWN-FAILING:` prefix even though the test passes today — trust the `FAIL` markers, not the name.
- Stale leftovers at the repo root: the `./autoindex` binary and `aindex/`, superseded by in-process autoindex. Both are **tracked in git**, so they need `git rm -r`. (`minimal_serv.cpp`, `webserv_epoll.cpp`, `parser/` and `incremental_versions/` are gone.)

## Subject scope & status

The assignment (`subject.txt`) is the 42 *webserv*: a non-blocking C++98 HTTP server driven by a single `epoll`, never reading/writing a socket/pipe without prior readiness, never checking `errno` after read/write, must not crash, GET/POST/DELETE, file upload, configurable error pages / body size / routes / redirects / directory listing / CGI by extension, multiple listen ports.

**The live list of gaps, bugs and their evidence lives in `docs/Gaps_and_Issues.md`** (snapshot 2026-08-05, `bug-hunt-2`, suite at 102 tests / 143 assertions / 14 failures across 11 distinct tests). That file is the one to update after a fix; this section only sketches the shape of the project so the architecture above reads in context. Issue numbers there are stable across snapshots, so the references above stay valid.

What is solid today:

- Single-`epoll` I/O for every socket and pipe; no `errno` inspected after read/write; SIGPIPE ignored; non-blocking sockets; `EINTR` tolerated; `FD_CLOEXEC` on every long-lived fd.
- Config parser + `http`/`server`/`location` tree + virtual-host resolution by `Host` (case-sensitively — see below).
- Internal-redirect / error-page loop bounded by `REDIRECT_LIMIT`, with parse-error statuses (400/501/505/414) preserved through setup.
- GET on static files (binary-clean), DELETE, directory listing generated in-process (no `fork` outside CGI), `index` appending to the directory path, correct `Content-Type` from the mime map.
- The CGI pipe/`rearm` state machine: LF-or-CRLF headers, case-insensitive `Status:`, EOF-driven finalize, 502 on a CGI that dies before its headers, `/tmp/cgi_*` unlinked.
- **CGI process lifetime**: nothing blocks in `waitpid`, a runaway is SIGTERM'd at `CGI_TIMEOUT` and SIGKILL'd after the grace period, aborted clients leave neither zombies nor leaked pipes.
- `client_max_body_size` → 413 on both CGI and static routes, with the oversized body drained before the response.
- A read-side idle timeout that answers `408 Request Timeout` rather than dropping the connection.
- A static POST path that writes an uploaded body to disk and answers 201 (first buffer only — see below).
- An exception escaping a connection is caught by the loop instead of killing the process.

What is not there yet, in one line each (details and reproduction steps in `Gaps_and_Issues.md`):

- **Startup, grade-0:** a missing/unreadable/directory config path spins forever at 100 % CPU; an empty config or one with no `http {}` segfaults; a garbage config throws out of `main`'s try/catch and aborts. `./webserv` with no argument hits the first of these (issue 25).
- **Features:** an upload *storage location* directive; built-in default error pages; a subject-format `README.md`; `client_body_timeout`; CGI selection by file extension. Chunked transfer-encoding is out of scope per the evaluators.
- **CGI, subject text:** no `HTTP_*` variables at all (nor `SERVER_PORT`/`SCRIPT_NAME`/`REMOTE_ADDR`), and no `chdir` into the script's directory (issues 27, 28).
- **Regressions to fix first:** POST skips the resolution loop (`limit_except` → 500 instead of 405, POST to a directory → 500); `Host` matching became case-sensitive when header values stopped being lowercased (issues 2, 26).
- **Hang / resilience class:** use-after-del in the event loop; `setup_cgi` failure leaving a connection unanswered; EMFILE busy-spin and a leaked fd when `set_nonblocking` throws; `_client_fd` never closed and pipe fds double-closed on teardown; the `fill_capacity() == 1` window (issues 6, 7, 10, 11, 17).
- **Correctness class:** POST bodies over one buffer truncated; internal `return` collapsing to 500; dead `client_header_buffer_size`; the post-setup timeout reset; weak `Content-Length`/version validation; missing reason phrases for 411/414/431/504; `Allow` on every response; duplicate headers dropped; `Location::operator=` dropping `cgi` (issues 1, 12, 13, 8, 14, 29, 19, 20, 31).
- **Compliance:** the autoindex temp file is never unlinked and its response has no `Content-Type`; `bzero`/`strncpy` vs. "prefer C++ versions"; dead code (`Connection.cpp`, `CgiConnection.*`, `ServerBlock.*`, `PidCollector.hpp`, `parse_cgi_headers`, the tracked `./autoindex` + `aindex/`). `mkstemp` was cleared with the evaluators.
