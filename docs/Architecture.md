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

> ⚠️ **The no-argument form exits 1.** There is still no `webserv.conf` at the repo root, so `./webserv` reports `cannot open config file webserv.conf` and returns 1 — pass a `conf/generated/*.conf` path. Every bad-config path now fails loudly rather than hanging: missing file, directory, empty file, no `http {}` block, garbage, a config with no listener, and an out-of-range port all exit 1 with a diagnostic (`Gaps_and_Issues.md` issues 25 and 33, fixed 2026-08-28).

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
`webserv.cpp`, `ConfigParser.cpp`, `Config.cpp`, `ServerConnection.cpp`, `ClientConnection.cpp`, `EpollLoop.cpp`, `Request.cpp`, `Response.cpp`, `ScratchBuffer.cpp`, `utils.cpp`, `autoindex.cpp`, `autoindex_File.cpp`.

**Dead code — all cleared 2026-08-28.** `src/Connection.cpp` (a second, conflicting inline `ClientConnection`), `src/CgiConnection.{cpp,hpp}`, `src/ServerBlock.{cpp,hpp}`, `inc/PidCollector.hpp` and `ClientConnection::parse_cgi_headers()` have been deleted, along with the tracked `./autoindex` binary and `aindex/`. Nine merge-conflict markers left in `ClientConnection.cpp` by the `origin/upload-directive` merge — which compiled only because they happened to sit inside comment blocks — are gone too. The one tracked artifact left is `tester` (7 MB); keep it only if it is the school's official tester.

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

> **On deletion within a batch:** `del()` does not delete. It inserts into `_deletion_queue`, which `clear()` drains only *after* the whole event batch **and** the timeout sweep, so a connection stays alive and keeps its fd for the rest of the batch; the queue is a `std::set`, so a repeated `del()` is idempotent. An event later in the batch can therefore still dispatch a connection that has already given up — wasteful, but not a dangling pointer. Earlier snapshots described this as a use-after-free; that was wrong, and `Gaps_and_Issues.md` issue 6 is retired. (`epoll_wait` `EINTR` was the other gap here; fixed 2026-08-05 — see `past_issues/EINTR_unhandled.md`.)

### Connection Hierarchy

```
Connection (abstract; owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
└── ClientConnection  — the whole request→response lifecycle (parsing, CGI, autoindex, response)
```

`ServerConnection::handle()` does a **single** `accept()` per readiness event (the multi-accept loop is commented out; correct under level-triggered epoll, slightly less efficient under bursts), then `set_nonblocking()` + `set_cloexec()` on the new fd, inside a try/catch that closes the accepted fd before rethrowing. A failed `accept()` returns, except on `EMFILE`/`ENFILE`: there the loop surrenders `EpollLoop`'s reserve fd (`release_reserve_fd()`), accepts and immediately closes the pending connection so the backlog entry drains and level-triggered readiness clears, then reclaims the reserve. Without that the listen fd stays readable forever — measured at 99 % CPU before the fix, 0 % after (`Gaps_and_Issues.md` issue 10, fixed 2026-08-28; reproducer in `spin.sh`).

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
- **Static POST** (no `cgi_pass` on the location): `setup_post()` `stat`s the target, then opens `upload_directory + path` with `out | trunc`, `handle_post_leftover()` flushes whatever body bytes already sat in `_buf`, and `handle_post()` continues in `REQ_BODY` feeding `_stream` until `content_length` is reached. The `stat` decides the status: **201 Created** when the file did not exist, **204 No Content** when it was replaced (RFC 9110 §9.3.3 only licenses 201 when a resource is actually created). Semantics are replace, not append — a repeated POST to the same URI overwrites, matching nginx's DAV PUT.
- **Autoindex is generated in-process** (`autoindex::as_html()` over `opendir`/`readdir`), written to a `/tmp/autoindex_XXXXXX` file, and served from the ordinary `RESPONSE` state. It no longer forks a helper and no longer borrows the CGI states; the `AUTOI_*` enum values are gone. The temp file is `std::remove`d as soon as it has been reopened for reading (the unlink-while-open idiom `finalize_cgi` uses), and the header block carries `Content-Type: text/html` (`Gaps_and_Issues.md` issue 4, fixed).
- A **CGI without a request body** (GET, or POST with `content_length == 0`) skips `REQ_BODY`/`CGI_TRANSMIT_BODY` entirely: `setup_cgi()` closes the stdin pipe immediately and `rearm()`s straight onto stdout in `CGI_HEADERS`.
- **POST runs the method check before its own branch.** `is_method_allowed()` is evaluated ahead of the POST/CGI split, so `limit_except` yields a proper 405 rather than the 500 it used to (`Gaps_and_Issues.md` issue 2, fixed).

`handle(events)` dispatches by state:
- `REQ_BODY` / `CGI_TRANSMIT_BODY` → `handle_cgi_input()` if the location has `cgi_pass`, otherwise `handle_post()` (client body → file)
- `CGI_HEADERS` / `CGI_BODY` → `handle_cgi_output()` (CGI stdout → temp file)
- `DISCARD_BODY` → read-and-drop until `content_length` reached, then `setup_res()`
- Otherwise, on `EPOLLIN`: fill `_buf` from the socket, then run `handle_req_line()` → `handle_req_headers()` → `handle_setup()` as the buffer accumulates.
- On `EPOLLOUT` in `RESPONSE`: `handle_response()` drains buffered headers + file to the socket; returns false when done → `epoll.del(this)`.

Key steps:
1. **Request line** (`handle_req_line`): validates method token / URI / version, percent-encoding, decodes and normalizes the path (`.`/`..` collapsing via `normalize_req_path`); sets `_req.status` (400/501/505/414 on failure). Bare `METHOD` with no URI is only accepted for POST (treated as HTTP/0.9).
2. **Headers** (`handle_req_headers` + `parse_req_headers`): lowercases keys only, splits `Host` into `hostname` + a validated port substring (the port itself is discarded), reads `Content-Length`; 411 if POST has no length; 431 if the header block overflows the buffer. Duplicate header lines are silently dropped by `map::insert`.
3. **Setup** (`handle_setup`): resolves the virtual server via `Host`; a non-zero `_req.status` carried over from parsing is preserved and routed straight to `epi_redirect()` (this is what makes 400/501/505/414 come out accurate). Otherwise it walks the `Location` tree in a loop bounded by `REDIRECT_LIMIT` (default 5), applying `return` directives, `index`, `autoindex`, method-allow and file-existence checks — for non-POST methods; POST takes the separate branch described above. DELETE is executed here (`std::remove` → 204). Error pages are resolved via `epi_redirect()` (internal redirect to the configured `error_page`, or a body-less response). The function ends by resetting `_timeout` from `_loc->get_header().timeout` — which is always the 60 s default, since `config::header` is not inherited into `Location` (`Gaps_and_Issues.md` issue 8, **still open**). The configured body timeout is nonetheless honoured in practice only because the static-POST branch returns before reaching that line; any path that does reach it silently reverts to 60 s.
4. **CGI** (`setup_cgi`): `pipe()`×2 (both ends `FD_CLOEXEC`) + `fork()` + `execve(interpreter, [interpreter, script], envp)`, with the pid handed to `EpollLoop::track_child()`. Env is `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `REQUEST_METHOD`, `SCRIPT_FILENAME`, `PATH_INFO`, `QUERY_STRING`, `SERVER_NAME`, `CONTENT_LENGTH`/`CONTENT_TYPE` on POST, plus any `cgi_param` pairs — and **nothing else**: no `HTTP_*`, no `SERVER_PORT`/`SCRIPT_NAME`/`REMOTE_ADDR`, and no `chdir` into the script's directory (`Gaps_and_Issues.md` issues 27 and 28, both subject requirements). Parent-side pipe ends are made non-blocking and the connection `rearm()`s onto them. POST bodies are streamed from the client buffer to CGI stdin; CGI stdout is buffered to a temp file (`/tmp/cgi_XXXXXX`), then the headers (incl. a case-insensitive `Status:` override) are split from the body. `finalize_cgi()` reopens the temp file for reading, unlinks it, and `rearm()`s back to the client fd — it **does not** `waitpid`; reaping is the loop's job.
5. **Static response** (`setup_res`): builds status line + headers (`Location`/`Allow`, `Date`, `Content-Length` from `stat`, and `Content-Type` from the location's `types`/`default_type` map keyed on the path's extension), opens the file via `_stream`, `mod()`s to `EPOLLOUT`. `setup_internal_error()` swaps in a static 500. `Location` and `Allow` are now independent conditions — `Allow` is emitted on 405 only, which RFC 9110 §15.5.6 makes a MUST there (issue 19, fixed) — and the reason-phrase map covers 411/414/431/504 (issue 29, fixed). When no `error_page` matches and the status is ≥ 400, `Response::default_error_page()` generates an HTML body which `buffer_inline_body()` streams from `_res_body` after the headers drain, in parallel with `buffer_file()`.

### ScratchBuffer

`ScratchBuffer` is a fixed-capacity byte buffer with two cursors: `readpos` (produce/write-in position) and `writepos` (consume position). `fill(...)` appends data **into** the buffer (from an fd, an `fstream`, or a literal); `feed(...)` drains data **out** of the buffer (to an fd or `fstream`). `fill_capacity()` is space remaining for `fill`; `feed_capacity()` is bytes pending to `feed`. `find(char/string)` searches the produced region; `erase(from,to)` memmoves to drop a span; `clear()` resets both cursors. `set_data()` lets it *reference* an external buffer (used for the static 500 string) instead of owning a heap buffer (`_ref_data`).

> Capacity comes from the matched server's `client_header_buffer_size` (default 1024). A request line / header block larger than this buffer yields 414/431 rather than growing.

> **Binary safety (fixed for the data path):** `fill(fstream)` now uses `fstream.read()`/`gcount()` (count-based), so served files — including the CGI temp-file body — pass through byte-clean; `feed(...)` was always count-based. The eof/fail semantics matter: `read()` sets eofbit+failbit on the final short read *after* `gcount()` bytes are delivered, which is what terminates `buffer_file` and satisfies `is_res_finished()`. Still string-oriented: `find()` uses `strnstr`/`strnchr` that stop at `\0` (only affects header *scanning*, and headers are text), the other `fill` overloads write a terminating `data[readpos]='\0'` (which is why `fill_capacity()` reserves one byte — keep the `-1`), and `fill_eof()`/`feed_eof()` compare against `'0'` (the digit) but appear unused.

### Request

`Request` (`Request.hpp`) fields: `method` (`HttpMethod` enum), `uri` (raw), `path` (normalized/decoded), `query`, `version`, `headers` (map — **keys** lowercased, values left case-intact), `content_length`, `host`/`hostname`, `status` (the current HTTP status being built toward), and the booleans `internal` (path is internal vs. an external URL) and `no_file` (build a body-less response). There is no `port` member any more — nothing read it, so it was deleted rather than wired into the CGI env.

> Since values stopped being lowercased, every consumer that compares one must do so case-insensitively itself (`equals_icase` lives in `utils.hpp`). `config::starts_with_scheme` and the CGI `Status:` check do, and so does the virtual-host lookup: `to_lower()` (in `utils.hpp`) folds a *local copy* of the incoming `Host` at the call site, and `server_name` values are lowered at parse time, so `Host: EXAMPLE.com` matches `server_name Example.com` in either direction. `_req.hostname` itself is left untouched, since it is forwarded to the CGI as `SERVER_NAME` (`Gaps_and_Issues.md` issue 26, fixed).

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
  Three namespaces were added in the 2026-07-29 pass as control/defect pairs; **all of them are green now** and read as regression guards: `upload_test.clj` (both a single-buffer and a 5 KB POST land at full length — `upload.conf`, which must carry an explicit `upload_directory` or the relative default resolves against the *test runner's* cwd), `autoindex_test.clj` (`autoindex.conf`), and `timeout_test.clj` (`timeout_body.conf`). The timeout tests use a local `send-and-idle` helper rather than `raw-request-timeout`, because half-closing the write side is detected on its own and would mask the timeout entirely. Added 2026-08-28: `cgi_header_buffer_test.clj` (`cgi_big_header.conf`) pins that a CGI header larger than the default buffer *is* forwarded once `client_header_buffer_size` is raised, the counterpart to `cgi_robustness_test`'s 502-under-the-default assertion. Removed: the chunked-encoding test, which documented an out-of-scope gap rather than a pending fix.
  The 2026-08-05 pass added `cgi_lifecycle_test.clj` (6 tests, all passing) covering the CGI process lifetime rewrite: a well-behaved CGI is answered and reaped, a runaway is killed at `CGI_TIMEOUT`, a SIGTERM-immune CGI is escalated to SIGKILL, the loop stays responsive while one runs away, and aborted requests leave neither zombies nor leaked fds.
  One reading note: many older test descriptions still start with a literal `KNOWN-FAILING:` prefix even though the test passes today — trust the `FAIL` markers, not the name.
- Stale leftovers at the repo root: the `./autoindex` binary and `aindex/`, superseded by in-process autoindex. Both are **tracked in git**, so they need `git rm -r`. (`minimal_serv.cpp`, `webserv_epoll.cpp`, `parser/` and `incremental_versions/` are gone.)

## Subject scope & status

The assignment (`subject.txt`) is the 42 *webserv*: a non-blocking C++98 HTTP server driven by a single `epoll`, never reading/writing a socket/pipe without prior readiness, never checking `errno` after read/write, must not crash, GET/POST/DELETE, file upload, configurable error pages / body size / routes / redirects / directory listing / CGI by extension, multiple listen ports.

**The live list of gaps, bugs and their evidence lives in `docs/Gaps_and_Issues.md`** (snapshot 2026-08-05, `bug-hunt-2`, suite at 102 tests / 143 assertions / 14 failures across 11 distinct tests). That file is the one to update after a fix; this section only sketches the shape of the project so the architecture above reads in context. Issue numbers there are stable across snapshots, so the references above stay valid.

What is solid today:

- Single-`epoll` I/O for every socket and pipe; no `errno` inspected after read/write; SIGPIPE ignored; non-blocking sockets; `EINTR` tolerated; `FD_CLOEXEC` on every long-lived fd.
- Config parser + `http`/`server`/`location` tree + virtual-host resolution by `Host` (case-insensitively, folded on both sides — see below).
- Internal-redirect / error-page loop bounded by `REDIRECT_LIMIT`, with parse-error statuses (400/501/505/414) preserved through setup.
- GET on static files (binary-clean), DELETE, directory listing generated in-process (no `fork` outside CGI), `index` appending to the directory path, correct `Content-Type` from the mime map.
- The CGI pipe/`rearm` state machine: LF-or-CRLF headers, case-insensitive `Status:`, EOF-driven finalize, 502 on a CGI that dies before its headers, `/tmp/cgi_*` unlinked.
- **CGI process lifetime**: nothing blocks in `waitpid`, a runaway is SIGTERM'd at `CGI_TIMEOUT` and SIGKILL'd after the grace period, aborted clients leave neither zombies nor leaked pipes.
- `client_max_body_size` → 413 on both CGI and static routes, with the oversized body drained before the response.
- A read-side idle timeout that answers `408 Request Timeout` rather than dropping the connection.
- A static POST path that writes an uploaded body to disk and answers 201 (first buffer only — see below).
- An exception escaping a connection is caught by the loop instead of killing the process.

What is not there yet, in one line each (details and reproduction steps in `Gaps_and_Issues.md`):

- ~~**Startup, grade-0**~~ — closed 2026-08-28 (issues 25, 33). All seven bad-config paths exit 1 with a diagnostic. Note that `Lexer::load` needs **both** an `is_open()` check and a `fail() && !eof()` check after the first read: a directory opens successfully and only fails on read, so `is_open()` alone would still hang on `./webserv conf`.
- **Features:** a subject-format `README.md` — the one item that gates the evaluation before anything runs — and CGI selection by file extension (issue 30). `client_body_timeout` is parsed and still unread at runtime. Upload storage location, built-in error pages and full CGI env/`chdir` are all done. Chunked transfer-encoding is out of scope per the evaluators.
- **Config fidelity:** `config::header` is not inherited Server→Location, and `Location::operator=` drops `cgi`/`header`/`output`, so any deep copy of a `Location` loses its `cgi_pass` (issues 8, 31). The configured body timeout works today only because the static-POST branch returns before the reassignment that would clobber it — the highest-value remaining code fix.
- **Hang class:** only the `fill_capacity() == 1` window remains (issue 11), review-only with no reproducer. Use-after-del (6) is retired as a misdiagnosis; `setup_cgi` failure (7), the EMFILE spin (10) and fd ownership (17) are fixed.
- **Compliance:** `bzero`/`strncpy` vs. "prefer C++ versions" (issue 22); `tester` is a tracked 7 MB binary and `$(ODIR)` should be an order-only prerequisite (issue 24). `mkstemp` was cleared with the evaluators. All the dead code listed in earlier snapshots is gone.
- **Test coverage, not code:** nothing exercises the config-robustness paths, the generated error-page body, or CGI env completeness. Issues 7 and 10 are verified by `sweep.sh` / `spin.sh` rather than by kaocha, which cannot set an rlimit on the server process.
