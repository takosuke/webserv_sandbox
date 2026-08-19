# Webserv — Missing Features & Open Issues

*Snapshot: 2026-08-05, branch `bug-hunt-2` (`64bfecf`). Test suite standing: `make test` → **102 tests, 143 assertions, 14 failures** (11 distinct tests). Previous snapshot (2026-07-29) was 96/127/16 (13 distinct); five of those tests now pass and a new `cgi_lifecycle_test.clj` namespace (6 tests) passes in full.*

*Scope decisions confirmed with the evaluators: **chunked transfer-encoding is not required**, and **`mkstemp` is acceptable** despite not being on the subject's function list. Both stay struck from the priority list; the chunked test stays in the suite as documentation, not as a blocker.*

Cross-checked against `Architecture.md`, `subject.txt`, `random.md`, the current sources, a clean `make re`, and live probes against a running server. **Issue numbers are stable across snapshots** — fixed items are struck in §0 and their numbers are retired, new findings get fresh numbers from 25 up, so references in `Architecture.md`, `TODO.md` and `random.md` keep pointing at the right thing.

Note on reading the test log: many test *names* still carry a `KNOWN-FAILING:` prefix baked into their description string. Only the lines marked `FAIL` are actually failing — most `KNOWN-FAILING`-named tests pass today.

---

## 0. Fixed since the 2026-07-29 snapshot

1. **Issue 3 — `Content-Type` is now correct** (commit `958f679`). `setup_res()` passes `substr(ext_del + 1)`, the map is hit, `GET /index.html` → `text/html`. *Tests now passing: both of `content-type-test`.*
2. **Issue 5 — `finalize_cgi()` no longer blocks the loop** (commits `6f4e2fa`, `c182976`). `waitpid` moved out of the response path entirely: `EpollLoop` keeps a `std::map<pid_t, Child>` child table (`track_child` / `kill_child` / `reap_children`), swept once per tick with `WNOHANG`, escalating SIGTERM → SIGKILL past the deadline. *Test now passing: `cgi-robustness-test/test-slow-exiting-cgi-does-not-block-the-loop`.*
3. **Issue 9 + feature 3's CGI half — CGI lifetime is managed.** `CGI_TIMEOUT` (10 s, `ClientConnection.hpp`) bounds a runaway CGI; `~ClientConnection` calls `kill_child(_cgi_pid)` so a client that aborts mid-POST no longer orphans its child; `~EpollLoop` SIGKILLs and reaps everything still tracked. *New namespace `cgi_lifecycle_test.clj`, 6/6 passing:* normal reap, SIGTERM-immune escalation to SIGKILL, aborted-request fd release, runaway killed at deadline, loop stays responsive during a runaway, no zombies from aborted requests.
4. **Issue 15 — only header *keys* are lowercased now** (commits `47386d4`, `c9e8e9b`). Values survive case-intact, so multipart boundaries and cookies are no longer corrupted. `equals_icase` moved into `utils.cpp` and `config::starts_with_scheme` was made case-insensitive and bounds-safe. **This fix has one unhandled consequence — see new issue 25.**
5. **Issue 16 — members are initialised** (commits `85ca0db`, `ecb00c0`). `_loc`, `_cgi_pid`, `_written_body` are in the `ClientConnection` ctor init list; `Request::port` was deleted outright since nothing read it.
6. **`EINTR` during `epoll_wait`** (commit `31f7b42`, was half of issue 6). Write-up in `docs/past_issues/EINTR_unhandled.md`. The use-after-del half of issue 6 is still open.
7. **Exceptions escaping a connection no longer kill the process** (commit `a815919`). `EpollLoop::run` wraps both `conn->handle()` and `clicon->handle_timeout()` in try/catch. This retires the "thrown `fcntl` exits the process" half of issue 10 — the EMFILE spin half is still open, plus a new fd leak on that path (issue 10, rewritten below).
8. **`FD_CLOEXEC` on every long-lived fd** (commit `64bfecf`, `docs/past_issues/FD_CLOEXEC.md`). Listen sockets (`SOCK_CLOEXEC`), accepted sockets (`set_cloexec`), the epoll fd (`EPOLL_CLOEXEC`) and both CGI pipe pairs. A CGI child no longer inherits another client's socket.
9. **`index` inside a subdirectory works.** *Test now passing: `static-index-test/test-subdirectory-index-is-served`.*
10. **Stale root artifacts partly gone.** `minimal_serv.cpp`, `webserv_epoll.cpp` and `parser/` no longer exist. `./autoindex` and `aindex/` are still there and are **tracked in git** — they need `git rm`, not just `rm` (issue 24).

---

## 1. Missing features (subject requirements)

In order of severity:

1. **File upload is implemented but not usable as specified.** What works: a POST to a static location writes its body to `root + path` and answers 201. What is missing or wrong:
   - **Bodies larger than one buffer are silently truncated** (issue 1) — the headline defect.
   - There is **no upload-storage-location directive**; the subject requires "storage location is provided". Uploads land wherever the URI points inside the location's `root`, so any POST is a write into the served document tree.
   - Semantics are append-only (`ios::app`). A repeated POST to the same URI grows the file instead of replacing it; there is no 409/204 distinction.
   - The 201 response echoes the *whole target file* back as the body (`setup_res` opens the same path for reading and sets `Content-Length` from `stat`). Verified live: `POST /scratch_probe.txt` with body `hello` → `201`, `Content-Length: 5`, body `hello`.
2. **The CGI does not receive the full request** (new issue 27) **and does not run in the script's directory** (new issue 28). Both are explicit subject lines under the CGI bullet. Details below — this is now the largest *feature* gap after upload.
3. ~~**Chunked transfer-encoding is not handled.**~~ **Out of scope** — confirmed with the evaluators. `grep -ri chunk src/ inc/` is still empty. `regression-test/test-chunked-post-unchunked-for-cgi` stays red on purpose as documentation of the gap.
4. **Timeout enforcement is still incomplete.** The 408 path works and a CGI runtime timeout now exists (fixed item 3 above), but:
   - The configured timeout is lost after setup (issue 8) — in practice every connection runs on the 60 s default from the moment the request line is parsed.
   - `client_body_timeout` is parsed and never read at runtime.
5. **No built-in default error pages.** When no `error_page` matches, `epi_redirect()` produces a body-less status-only response. Verified live: `GET /<2000 a's>` → `HTTP/1.0 414 Unknown Status Code\r\nAllow: …\r\nDate: …\r\n\r\n` — no body, and no reason phrase either (issue 29). The subject requires "default error pages if none are provided".
6. **No subject-compliant `README.md`.** The required shape (italic first line "*This project has been created as part of the 42 curriculum by …*", Description / Instructions / Resources incl. an AI-usage description) is absent; `README.md` is still internal design notes about response caching.

---

## 2. Open issues

In order of severity.

### Critical — the server crashes or hangs before it ever serves a request (grade-0)

25. **A malformed, missing or unreadable config file crashes or wedges the process.** Four reproducers, all found this pass, none previously documented. The subject's first general rule is "must not crash under any circumstances"; the third requirement is "must use a configuration file, provided as an argument … or available in a default path". An evaluator typing a wrong path or hand-writing a config hits these immediately.

    | Input | Result | Cause |
    |---|---|---|
    | `./webserv /nope/nothere.conf` | **infinite spin, 100 % CPU, never exits** | `Lexer::load` (`ConfigParser.cpp:181-191`) never checks `stream.is_open()`. On a failed open `failbit` is set but `eofbit` is not, so `lex_file`'s `while (!stream.eof())` (`:203`) never terminates. |
    | `./webserv` with no argument | same infinite spin | the default path is `"webserv.conf"` (`webserv.cpp:24`) and no such file exists at the repo root — the *documented* invocation hangs. |
    | `./webserv conf` (a directory) | same infinite spin | same code path. |
    | empty file, or any config with no `http {}` block | **SIGSEGV** | `webserv.cpp:41` does `grouper.main.body_directives[0]` without checking `.empty()`. `operator[]` out of range → garbage reference → crash inside `Http::from_directive`. Backtrace confirmed under gdb. |
    | `this is not a config {{{ ;;;` | **uncaught `std::length_error` → `terminate` → abort** | thrown from `Directive::Directive(std::vector<Token>&)` via `Grouper::group()`, called at `webserv.cpp:33-37` — **outside** the try/catch that starts at `:40`. |

    Three-part fix, all in the startup path: check `is_open()` in `Lexer::load` and fail loudly; move `Grouper grouper(path); grouper.group();` inside the existing try/catch; guard `body_directives` for emptiness before indexing. No test covers any of this yet — a `config_robustness_test.clj` that shells out to `./webserv <bad file>` with a timeout would pin all five rows.

26. **`Host` matching became case-sensitive — a virtual-host regression from the header-case fix.** `_req.hostname` is passed raw to `http->get_server(_addr, …)` (`ClientConnection.cpp:569-570`) and `Port::get_server_by_name` (`Config.cpp:1542`) is a plain `std::map` lookup. Before fixed item 4 the blanket lowercase made this work by accident. Verified live against `virtual_hosts.conf`: `Host: static.local` → `www/static/index.html` (338 bytes), `Host: STATIC.LOCAL` → the **default** server's `www/index.html` (291 bytes). Host is case-insensitive per RFC 7230 §2.7.3. Fix at the call site — fold a *local* lowercase copy; don't mutate `_req.hostname`, which is forwarded to the CGI as `SERVER_NAME` and should reflect what the client sent. (`random.md` predicted this exact breakage.)

### Regressions and defects in the POST/upload path

1. **Static uploads are truncated to the first buffer.** Verified live: `POST /up.txt` with `Content-Length: 5000` writes **945 bytes**, answers `201` early, and resets the connection. Cause: in `handle_post()`, the `_state == REQ_BODY` branch (`ClientConnection.cpp:1026-1045`) reads from the socket and then does `if (_buf.feed_capacity() > 0) { _state = RESPONSE; setup_res(); }` — it jumps to the response instead of feeding the buffer to `_stream`. The feeding half of `handle_post()` below that branch is only reachable in `CGI_TRANSMIT_BODY`, which a static POST never enters. Only the leftover bytes written by `handle_post_leftover()` during setup ever reach disk. *(Failing test: `upload-test/test-large-upload-is-not-truncated`, `conf/upload.conf`. `test-small-upload-reaches-disk` is the passing control.)*

2. **`limit_except` does not apply to POST — 405 became 500.** `handle_setup()` runs `if (_req.method == POST) { … } else while (…)` (`ClientConnection.cpp:582-660`), so a POST skips the whole resolution loop: no `is_method_allowed()`, no `is_file_existing()`, no `return`/`index`/`autoindex` handling, no directory check. Consequences:
   - `POST /readonly` on a `limit_except GET` location returns **500**, not 405. *(Failing test: `limit-except-test/test-post-blocked-on-restricted-location`.)*
   - `POST /` (a directory) returns 500 — `set_file` cannot open a directory for append. Re-verified live this pass.
   - `return` and `error_page`-driven internal redirects do not fire for POST.
   - Path traversal is *not* a problem here: `normalize_req_path` collapses `..` before resolution, so `POST /../escaped.txt` stays inside the root.

4. **Autoindex responses leak a temp file per request and carry no `Content-Type`.** `setup_autoindex()` (`ClientConnection.cpp:1108-1139`) generates the listing in-process (good) but writes it to `/tmp/autoindex_XXXXXX` and never unlinks it — unlike `finalize_cgi`, which removes its `/tmp/cgi_*`. Staging through a temp file is fine (`mkstemp` is cleared); leaking it is not. It also builds its headers by hand and never adds `Content-Type: text/html`. Both are fixed by unlinking right after reopening for read (the unlink-while-open idiom already used for CGI) and adding the header. *(Failing tests: `autoindex-test/test-autoindex-does-not-leak-temp-files` — 5 requests, 5 files left in `/tmp` — and `test-autoindex-has-html-content-type`, `conf/autoindex.conf`. `test-autoindex-lists-directory` is the passing control.)*

### CGI feature gaps (subject text, not just polish)

27. **The CGI environment is missing every `HTTP_*` variable, plus `SERVER_PORT`, `SCRIPT_NAME`, `REMOTE_ADDR` and `SERVER_SOFTWARE`; `PATH_INFO` holds what `SCRIPT_NAME` should.** Verified live — the complete env a script sees today is:
    ```
    GATEWAY_INTERFACE=CGI/1.1   SERVER_PROTOCOL=HTTP/1.1   REQUEST_METHOD=GET
    SCRIPT_FILENAME=<root><path>   PATH_INFO=/cgi-bin/probe.py   QUERY_STRING=foo=bar
    SERVER_NAME=localhost   [+ CONTENT_LENGTH / CONTENT_TYPE on POST, + cgi_param pairs]
    ```
    A `Cookie:` and a `User-Agent:` sent with the request appear nowhere. The subject says "the full request and arguments provided by the client must be available to the CGI" — with no `HTTP_*` a CGI cannot do sessions, auth or content negotiation at all. Do the whole `TODO` block at `ClientConnection.cpp:793-796` in one pass, in this order of value: **`HTTP_*` (uppercase the key, `-`→`_`, prefix `HTTP_`)**, then `SCRIPT_NAME`/`PATH_INFO` split (per RFC 3875 `SCRIPT_NAME` is the script, `PATH_INFO` only the trailing remainder), then `SERVER_PORT` (`ntohs(_addr.sin_port)`), `REMOTE_ADDR` (needs plumbing — `accept()`'s `client_addr` is filled and discarded at `ServerConnection.cpp:19-24`; pass it through the ctor or call `getpeername`), `SERVER_SOFTWARE`, and `REDIRECT_STATUS=200` if PHP is ever a target.

28. **The CGI is not run in the script's directory.** No `chdir` exists anywhere in `src/` or `inc/`. Verified live: a CGI printing `os.getcwd()` reports the **server's** working directory, not `www/cgi-bin/`. The subject states "The CGI should be run in the correct directory for relative path file access", so any script opening a file by relative path breaks. Fix is two lines in the child branch of `setup_cgi()` (`ClientConnection.cpp:854-864`): `chdir()` to the script's dirname before `execve`, and pass the basename as `argv[1]`. `chdir` is on the allowed-functions list.

29. **The reason-phrase map is missing codes the server actually emits.** `Response::init_reason_phrase_map` (`Response.cpp:18-45`) has no entry for **411**, **414**, **431** or **504**, all of which `ClientConnection` sets. Verified live: an overlong URI yields the literal status line `HTTP/1.0 414 Unknown Status Code`. Four `insert` lines. (Also fix the `"HTTP Version Not SUpported"` typo at `:39` while in there.)

18. **Oversized CGI header block → 502 instead of forwarded.** Policy decision still pending (forwarding needs growable header buffering). Keeping the 502 is defensible — if that's the call, retire the test rather than leave it red. *(Failing test: `cgi-robustness-test/test-oversized-cgi-header-is-forwarded`.)*

30. **CGI is selected per-location via `cgi_pass`, not by file extension.** The subject phrases the requirement as "Execution of CGI, based on file extension (for example .php)". A `location /cgi-bin` block covers the demo, but a reviewer who writes a rule keyed on `.php` will not get one. Cheap middle ground: accept an optional extension list on `cgi_pass` (or a `cgi_ext` directive) and check the request path's extension before forking.

### Critical — hang / DoS class at runtime (grade-0 risk)

6. **EpollLoop: use-after-del within a batch** (`EpollLoop.cpp:111-125`, still an in-source TODO). An event later in the same `epoll_wait` batch can dereference a connection already `del()`'d — the loop does not re-check `_connections.count(fd)` before dispatching. The new try/catch around `conn->handle()` does **not** cover this: a dangling `this` is undefined behaviour, not an exception.

7. **`setup_cgi()` failure leaves the connection hung forever** (`ClientConnection.cpp:686-689`). On pipe/fork failure `handle_setup` sets `_req.status = 500` and `return (true)` without calling `setup_res()` or `mod()`ing to `EPOLLOUT`; the client waits until the 60 s timeout, and `handle_timeout` then `del()`s it without a response because `_state` is already past `DISCARD_BODY`.

8. **The configured timeout is discarded after setup.** `handle_setup()` ends with `_timeout = _loc->get_header().timeout` (`ClientConnection.cpp:667`), but `config::header` is **not inherited** Server→Location: `Location::from_server()` copies `root`, `body`, `output`, `mime`, `errorpages`, `index`, `autoindex` — not `header` (`Config.cpp:1186-1194`) — and `Location::operator=` omits it too (issue 31). `client_header_timeout` is also rejected inside a `location` block by the parser, so a location's `header.timeout` is always the 60 s default. Net effect: the configured timeout only governs the request-line/header phase (set from the *server* in the ctor); from `handle_setup()` onward every connection silently reverts to 60 s, holding an fd and a half-written upload for a minute. *(Failing test: `timeout-test/test-stalled-body-times-out`, `conf/timeout_body.conf`. `test-stalled-headers-time-out` is the passing control, proving the value is honoured before setup. Neither test half-closes: an EOF is detected on its own and would mask the timeout.)*

10. **Accept-path fragility (rewritten — half of this was fixed).** `accept()` returning −1 on EMFILE leaves the listen fd `EPOLLIN`-ready under level-triggered epoll → busy-spin at ~200 wakeups/s until an fd frees (`ServerConnection.cpp:21-22`, still `// TODO handle error?`). The fix is to keep a reserve fd to accept-and-close with, or `EPOLL_CTL_DEL` the listener until pressure drops. *Newly noticed:* `set_nonblocking`/`set_cloexec` throwing now unwinds into `EpollLoop::run`'s catch instead of exiting the process (good), but `client_fd` has already been accepted at that point and is never closed — under fd pressure each throw leaks one more socket, tightening the loop.

11. **Off-by-one deadlock window at `fill_capacity() == 1`.** The read guards require `> 1` (`ClientConnection.cpp:166, 890, 985, 1032`), the 414/431 guards require `<= 0` (`:435, :479`); at exactly 1 neither fires → no read, no error, readable socket → spin.

17. **Destructor / fd bookkeeping.** `~ClientConnection` (`ClientConnection.cpp:117-124`) closes `_cgi_stdin_fd` and `_cgi_stdout_fd` and hands `_cgi_pid` to `kill_child`, but never closes `_client_fd`. Meanwhile `EpollLoop::delete_conn` closes `conn->fd` — which during a CGI phase *is* one of the pipe fds, because `rearm()` swapped it. So a connection torn down mid-CGI **leaks its client socket** and **double-closes** whichever pipe fd `delete_conn` just closed. (Benign today only because nothing opens an fd between the `close` and the `delete`.) The clean fix is to close all three explicitly with per-fd `!= -1` guards and have `delete_conn` set `conn->fd = -1` after closing. *`cgi-lifecycle-test/test-aborted-cgi-requests-do-not-leak-fds` passes, so this is not firing on the abort path it exercises — but the exception-unwind path added in fixed item 7 reaches teardown from anywhere, which is exactly when it bites.*

### Correctness — wrong status codes / broken config features

12. **Internal `return <code> <path>` always yields 500.** The `return` branch (`ClientConnection.cpp:605-620`) sets `_req.path` but never re-resolves `_loc`, so the same location's `return` fires every iteration until `REDIRECT_LIMIT` forces a 500. Only external returns work. *(Failing test: `redirect-test/test-internal-return-redirects`.)*

13. **`client_header_buffer_size` is still dead — `set_capacity()` never assigns `capacity`.** The function allocates and copies but the `capacity = cap;` line only exists in `set_data()` (`ScratchBuffer.cpp:38-62`), so the 1024 default always wins; a 3 KB request line under a 16 KB configured buffer still yields 414. Its `std::min(sizeof(data), sizeof(new_data))` also copies 8 pointer-bytes, not buffer contents (harmless, pre-fill only). *(Failing test: `redirect-test/test-large-header-buffer-honored`.)*

14. **Weak value validation → wrong status.** `Content-Length: 123abc` and the bogus minor version `HTTP/1.10` are both accepted instead of yielding 400: the version check uses `find_first_not_of('0', 8)` (`ClientConnection.cpp:411`), and `Content-Length` is parsed with `istringstream >>` which stops at the junk without failing (`:508-512`). A fully unparseable value returns 500, a client error reported as a server error. *(Failing tests: `parse-error-test/test-content-length-trailing-junk-is-400`, `test-bogus-minor-version-is-400`.)*

19. **`Allow` header emitted on every internal response** — 200s, 201s, 404s, 500s and 502s all carry `Allow: GET, POST, DELETE`; it belongs on 405 only. Re-verified on every probe this pass (`setup_res` `:749`, `setup_autoindex` `:1128`).

20. **Duplicate request headers silently dropped** (FIXME at `ClientConnection.cpp:470`). `map::insert` keeps the first and discards the rest with no diagnostic. Decide a policy (first-wins like NGINX, or comma-fold) and make it deliberate.

21. **`autoindex` takes `true`/`false`, not nginx's `on`/`off`.** `autoindex on;` is a hard config error (`Config.cpp:951-959`). Harmless but surprising at evaluation, where a reviewer will type `on`; accepting both costs two lines.

31. **`Location::operator=` silently drops `cgi`, `header` and `output`** (`Config.cpp:1025-1040`). The copy ctor delegates to it, and `copy_deep_container` (`Config.hpp:335-346`) copy-constructs, so **any deep copy of a `Location` loses its `cgi_pass`**. Latent rather than live — the parse path builds locations in place today — but it is the same class of bug as issue 8's missing `header` inheritance and it will surface the moment a `Server` or `Http` is copied. Same rule as `from_server`: anything added to the structs must be added to *both* places.

32. **`HTTP/1.1` with no `Host` header is served 200.** RFC 7230 §5.4 requires 400. Verified live. Debatable given the subject names HTTP/1.0 as the reference point — but "status codes must be accurate" is an explicit grading line, and it is a one-line check next to the existing `Host` parsing.

33. **A config that produces no listener starts anyway.** `http { }` (no `server`) and `listen 127.0.0.1:99999` both leave the event loop running with zero sockets registered, spinning on the 5 ms tick forever with no diagnostic. Print an error and exit non-zero when `ports` is empty, and range-check the port at parse time.

### Compliance / housekeeping

22. ~~**`mkstemp` is not on the allowed-functions list**~~ — **cleared with the evaluators**. Unrelated and still open: `bzero`/`strncpy` (`ScratchBuffer.cpp:7, 47, 82`, `Config.cpp`) clash with "always prefer their C++ versions" — `std::memset`/`std::memcpy` are drop-in.

23. **5 ms `epoll_wait` tick** wakes the process ~200×/s even when idle — acceptable, but a nearest-deadline computation (or a coarser tick) would be cleaner, and it is what makes issues 10, 25 and 33 present as 100 %-CPU spins rather than quiet stalls.

24. **Dead code and stale artifacts.**
    - `src/Connection.cpp` — a *second, conflicting* inline `class ClientConnection`; not in the Makefile, so no ODR clash today, but a footgun.
    - `src/CgiConnection.cpp` + `inc/CgiConnection.hpp` — superseded by in-`ClientConnection` CGI.
    - `src/ServerBlock.cpp` + `inc/ServerBlock.hpp` — a 3-line stub that is **compiled into the binary** and used by nothing (`webserv.cpp`'s include is already commented out).
    - `inc/PidCollector.hpp` — **new dead file**, and it does not even compile: no `<set>` include, no semicolon after the class. Superseded by `EpollLoop::_children` before it was ever used. Delete.
    - `ClientConnection::parse_cgi_headers()` (`:1062-1090`) — declared, defined, never called; still contains the old case-sensitive `key == "Status"` bug next to the correct `equals_icase` version in `handle_cgi_output`.
    - The commented-out multi-accept loop and the commented-out `REQ_BODY` block at `ClientConnection.cpp:185-189`.
    - `./autoindex` (binary) and `aindex/` at the repo root — **tracked in git**, so `git rm -r` them.
    - Makefile: `$(NAME): $(ODIR) $(OBJS)` should be an order-only prerequisite (`$(ODIR)%.o: %.cpp | $(ODIR)`); the clang-detect `ifeq` at `:54` is shell syntax inside a make conditional and is never true.

---

## 3. Current test failures at a glance

11 distinct tests, 14 assertions.

| Test | Symptom | Issue |
|---|---|---|
| `upload/test-large-upload-is-not-truncated` (×2) | 5000-byte body lands as 945 | 1 |
| `limit-except/test-post-blocked-on-restricted-location` | 500, expected 405 | 2 |
| `autoindex/test-autoindex-does-not-leak-temp-files` | 5 requests → 5 files in `/tmp` | 4 |
| `autoindex/test-autoindex-has-html-content-type` | header absent | 4 |
| `timeout/test-stalled-body-times-out` | still connected after 6 s | 8 |
| `redirect/test-internal-return-redirects` (×2) | 500, expected 301 | 12 |
| `redirect/test-large-header-buffer-honored` | 414, expected 404 | 13 |
| `parse-error/test-content-length-trailing-junk-is-400` | 200, expected 400 | 14 |
| `parse-error/test-bogus-minor-version-is-400` | 200, expected 400 | 14 |
| `cgi-robustness/test-oversized-cgi-header-is-forwarded` (×2) | 502, expected forwarded | 18 |
| `regression/test-chunked-post-unchunked-for-cgi` | empty body | out of scope — chunked not required |

**Uncovered by any test** — everything found by code review or hand probes this pass and last:
- the whole config-robustness class (25) — the biggest untested surface, and the cheapest to pin: shell out to `./webserv <bad file>` under a timeout and assert a non-zero exit within a second.
- case-insensitive `Host` (26) — one request against `virtual_hosts.conf` with an uppercased `Host`.
- CGI env completeness (27) and CGI working directory (28) — `www/cgi-bin/probe.py`-style scripts asserting on `HTTP_COOKIE` and `os.getcwd()`.
- reason phrases for 411/414/431 (29).
- POST to a directory → 500 (2, same root cause as the `limit_except` failure).
- `Allow` on non-405 responses (19), no-`Host` HTTP/1.1 (32).
- code-review-only: 6, 7, 10, 11, 17, 31, 33.

---

## 4. Priority order

Ordered by *grade risk per unit of work*, not by how interesting the bug is. Struck by the evaluators' rulings: chunked encoding, and the `mkstemp` objection.

### Phase 0 — cheap items that gate the evaluation (an afternoon)

| # | Item | Issue | Why now |
|---|---|---|---|
| 1 | Config-file robustness: `is_open()` check, `group()` inside the try/catch, `body_directives` emptiness guard | **25** | Three small edits close two infinite hangs, a SIGSEGV and an abort. `./webserv` with no arguments — the invocation printed in the README-to-be — hangs today. Nothing else on this list survives a grade-0. |
| 2 | `README.md` in the subject's format | feature 6 | Gates the evaluation before a single request is sent, and costs an hour. |
| 3 | Lowercase a local copy of `Host` before vhost lookup | **26** | A regression introduced by the last fix; one line at `ClientConnection.cpp:570`. |
| 4 | Restore the resolution checks for POST | 2 | `limit_except` no longer blocks POST (500 instead of 405) and POST to a directory 500s. An evaluator types both. Clears 1 failure. |
| 5 | Autoindex: unlink the temp file, add `Content-Type: text/html` | 4 | Two lines, using the unlink-while-open idiom already in `finalize_cgi`. Clears 2 failures. |
| 6 | Add 411/414/431/504 reason phrases (+ the `SUpported` typo) | **29** | Four lines. `414 Unknown Status Code` on the wire is the kind of thing a reviewer screenshots. |

### Phase 1 — broken subject features (the bulk of the grade)

| # | Item | Issue | Why |
|---|---|---|---|
| 7 | Stream the whole POST body to disk | 1 | "Clients must be able to upload files" is a subject line and the demo is *upload a file, then download it back*. Anything over ~1 KB is silently corrupted today. Clears 1 failure. |
| 8 | CGI env: `HTTP_*`, then `SCRIPT_NAME`/`PATH_INFO`, `SERVER_PORT`, `REMOTE_ADDR`, `SERVER_SOFTWARE` | **27** | "The full request … must be available to the CGI" is explicit. Without `HTTP_*` no CGI can read a cookie. Do the whole `TODO` block in one pass. |
| 9 | `chdir` into the script's directory before `execve` | **28** | Explicit subject line; two lines in the child branch. |
| 10 | Upload storage location (an `upload_store`-style directive + routing) | feature 1 | The subject asks for the storage location to be configurable; today a POST writes into the served document tree at whatever path the URI names. |
| 11 | Built-in default error pages | feature 5 | Explicit subject requirement; today an unmatched code yields a body-less response. Self-contained: one static HTML template in `Response`. |
| 12 | Internal `return <code> <path>` | 12 | "HTTP redirection" is a required per-route rule. External returns work, internal ones 500. Clears 1 failure. |

### Phase 2 — hang / crash class ("must remain operational")

| # | Item | Issue | Why |
|---|---|---|---|
| 13 | Stop discarding the configured timeout after setup | 8 | Either inherit `config::header` into `Location` (and add it to `operator=`, issue 31) or drop the reassignment in `handle_setup`. Also the place to wire `client_body_timeout`. Clears 1 failure. |
| 14 | `setup_cgi()` failure → build a 500 response instead of hanging | 7 | Three lines: call `setup_res()` on the failure path. |
| 15 | EpollLoop use-after-del in a batch | 6 | Re-check `_connections.count(fd)` before dispatching. A latent crash the new try/catch does *not* cover. |
| 16 | Destructor fd bookkeeping: close `_client_fd`, stop double-closing pipes | 17 | Now that teardown can happen from anywhere (exception unwind), the tidy end-states the destructor assumed no longer hold. |
| 17 | `accept`/EMFILE handling + close the fd when `set_nonblocking` throws | 10 | Under fd pressure the server busy-spins and leaks a socket per failure. |
| 18 | `fill_capacity() == 1` window | 11 | Cheap hygiene with a real hang tail; batch with 16. |

### Phase 3 — status-code accuracy and config fidelity

| # | Item | Issue | Why |
|---|---|---|---|
| 19 | Reject `Content-Length: 123abc` and `HTTP/1.10` with 400 | 14 | "Your HTTP response status codes must be accurate." Clears 2 failures. |
| 20 | `set_capacity()` must assign `capacity` | 13 | One line; makes `client_header_buffer_size` real. Clears 1 failure. |
| 21 | `Allow` only on 405 | 19 | Visible in every NGINX side-by-side comparison. |
| 22 | Add `cgi`/`header`/`output` to `Location::operator=` | **31** | One-line class of bug that already bit once as issue 8. |
| 23 | Accept `autoindex on\|off` as well as `true\|false` | 21 | Two lines; avoids a config-file surprise at the demo. |
| 24 | CGI selection by file extension | **30** | The subject phrases the requirement that way; a reviewer may write the rule that way. |
| 25 | Duplicate request headers | 20 | Decide a policy (first-wins, like NGINX) and stop dropping silently. |
| 26 | 400 on HTTP/1.1 without `Host`; error out on a config with no listener | **32**, **33** | Two small accuracy fixes. |

### Phase 4 — housekeeping (do last, or in idle moments)

Decide the oversized-CGI-header policy (issue 18 — keeping the 502 is defensible; if so, retire that test rather than leave it red), delete the dead code (`Connection.cpp`, `CgiConnection.*`, `ServerBlock.*`, `PidCollector.hpp`, `parse_cgi_headers`, and `git rm -r` the tracked `./autoindex` binary and `aindex/`), replace `bzero`/`strncpy` with `std::memset`/`std::memcpy`, make `$(ODIR)` an order-only prerequisite, drop the dead clang `ifeq`, and consider computing the nearest deadline instead of the 5 ms tick.

**Out of scope:** chunked transfer-encoding (`regression-test/test-chunked-post-unchunked-for-cgi` stays red on purpose) and the `mkstemp` compliance objection.
