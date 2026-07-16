# Webserv — Missing Features & Open Issues

*Snapshot: 2026-07-15, branch `bug-hunt` (f0fea30). Test suite standing: `make test` → **89 tests, 113 assertions, 32 failures**.*

Cross-checked against `Architecture.md`, `subject.txt`, the current sources, and a live run. Items Architecture.md lists that are **already fixed** and therefore *not* repeated below: DELETE works; binary files serve intact; CGI env `CONTENT_LENGTH`/`CONTENT_TYPE`; the whole `handle_cgi_output` rewrite family (LF headers, case-insensitive `Status:`, blank-line consumption, EOF-driven finalize, 502 on dead-before-headers); `/tmp/cgi_*` unlink; merge-conflict markers in `ServerConnection.cpp` (gone); and — new since the doc was written — a **partial** timeout mechanism exists (`EpollLoop::run` now ticks at 5 ms and sweeps `_last_update`/`_timeout`; Architecture.md's "epoll_wait blocks with timeout `-1`" is stale). The timeout branch is known-incomplete; its gaps are itemized below.

---

## 1. Missing features (subject requirements)

In order of severity:

1. **File upload (non-CGI POST) is unimplemented.** `is_method_allowed()` (`ClientConnection.cpp:483-488`) rejects POST on any location without `cgi_pass`; there is no upload-storage-location config and no code path that writes a body to disk. Subject: "Clients must be able to upload files" + "storage location is provided". Side effect: `client_max_body_size`/413 is unreachable on static routes (see issue 12).

2. **Chunked transfer-encoding is not handled.** No un-chunking code exists anywhere (`grep -ri chunk src/ inc/` is empty); only `Content-Length` bodies are read. Subject explicitly requires un-chunking chunked requests before handing the body to CGI. *(Failing test: `regression-test/test-chunked-post-unchunked-for-cgi`.)*

3. **Timeout enforcement is incomplete** (timeout branch merged but unfinished). What exists: a 5 ms `epoll_wait` tick and a per-connection sweep in `EpollLoop::run()` (`EpollLoop.cpp:114-120`) against `_timeout` (default 60 s, from `client_header_timeout`). What's missing:
   - `_last_update` is refreshed at the top of **every** `handle()` call (`ClientConnection.cpp:124`) even when zero progress is made, so any connection stuck in a readable-EOF spin (half-close, `%25` loop) **never times out** — the timeout doesn't bound the two worst hangs (see issues 1–2 below).
   - Timed-out connections are silently closed — no `408 Request Timeout` response.
   - `client_body_timeout` is parsed but never read at runtime (only `get_header().timeout` is used, at `ClientConnection.cpp:112` and `:618`).
   - No CGI runtime timeout: an immortal CGI holds its connection forever, and `finalize_cgi`'s blocking `waitpid` (issue 4) is unbounded.
   - Subject: "A request to your server should never hang indefinitely."

4. **No `Content-Type` on any response; `types` / `default_type` are parsed-but-unused.** `setup_res` emits `Allow`/`Date`/`Content-Length` but never a MIME type, so "serve a fully static website" is only partially met (browsers sniff HTML but mishandle CSS/JS/images). *(Failing tests: `content-type-test/*`.)*

5. **No subject-compliant `README.md`.** The required format (italic first line "*This project has been created as part of the 42 curriculum by …*", Description / Instructions / Resources incl. AI-usage sections) is absent; the current `README.md` is internal design notes.

6. **No built-in default error pages.** The subject requires "default error pages if none are provided"; when no `error_page` matches, `epi_redirect()` produces a body-less status-only response rather than a default HTML page.

---

## 2. Open issues

In order of severity.

### Critical — crash / hang / DoS class (grade-0 risk)

1. **Half-close busy-loop — still live, re-verified today.** A client that sends a partial request and `shutdown(SHUT_WR)` pegs a core at ~80–95 % CPU indefinitely: `read()` returns 0 at EOF, the socket stays EPOLLIN-ready, and `handle()` never checks for the EOF to `del()` the connection (`ClientConnection.cpp:156-161`, `ScratchBuffer.cpp:64-70`). The resilience tests **pass** because the server stays responsive while burning the core — and the new timeout can't reap it because the spin refreshes `_last_update` every iteration. `DISCARD_BODY` has the same spin plus no cap on how many bytes it will drain (`ClientConnection.cpp:136-151`). A handful of such clients is a trivial DoS.

2. **`decode_http()` infinite loop on URIs decoding to a literal `%`.** `GET /%25` (or `/%2525`) still hangs: the `find('%')` loop never advances past an invalid escape (`ClientConnection.cpp:282-293`). Same family: `decode_hex` indexes `hex_val[256]` with a sign-extended `char` cast (UB on high-bit bytes) and `new_uri[pos+1]`/`[pos+2]` are read without bounds checks. *(Failing tests: `resilience-test/test-percent-encoded-percent-does-not-loop`, `test-double-encoded-percent-does-not-loop`.)*

3. **`ScratchBuffer` signed→unsigned wrap on failed `read`/`write`.** `feed(int fd)`: `writeret` is `size_t`, so `write()` returning −1 becomes `SIZE_MAX`, passes the `> 0` guard, and `writepos += SIZE_MAX` wraps; the next `feed` calls `write` with a huge length and an out-of-bounds pointer → likely crash (`ScratchBuffer.cpp:89-93`). Trigger is ordinary: client disconnects mid-response (EPIPE with SIGPIPE ignored) or CGI closes stdin early. `fill(int fd)` (`:64-70`) has the same hazard, survived only because callers store the result in an `int`.

4. **`finalize_cgi()` blocks the event loop in `waitpid(pid, NULL, 0)`** (`ClientConnection.cpp:956`, and the copy in the 502 branch at `:838` — both marked FIXME). A CGI that closes stdout but keeps running stalls every other client until it exits. Needs `WNOHANG` + deferred reap, or `kill` then reap. *(Failing test: `cgi-robustness-test/test-slow-exiting-cgi-does-not-block-the-loop`.)*

5. **EpollLoop: use-after-del within a batch, and `EINTR` unhandled.** Both still open as in-source TODOs (`EpollLoop.cpp:98-109`): an event later in the same `epoll_wait` batch can dereference a connection already `del()`'d, and a signal during `epoll_wait` breaks the loop instead of continuing.

6. **`setup_cgi()` failure leaves the connection hung forever.** On pipe/fork failure `handle_setup` sets `_req.status = 500` and `return (true)` without ever calling `setup_res()` or `mod()`ing to EPOLLOUT (`ClientConnection.cpp:637-640`); the client waits indefinitely.

7. **Zombie CGI processes on client abort.** No `kill()` exists anywhere in `src/`; if the client dies mid-POST, the forked child is never killed or reaped — the destructor closes only pipe fds (`ClientConnection.cpp:116-121`). Zombies accumulate for the server's lifetime. Compounded by `_cgi_pid` being uninitialized (issue 17): `waitpid` on a garbage pid could reap an unrelated child.

8. **Accept-path fragility.** `set_nonblocking()` throws on `fcntl` failure and nothing between `conn->handle()` and `main()` catches it → process exit; `accept()` returning −1 on EMFILE leaves the listen fd EPOLLIN-ready → busy-spin until an fd frees (`ServerConnection.cpp:20-27`, `utils.cpp:10-16`).

9. **Off-by-one deadlock window at `fill_capacity() == 1`.** The read guard requires `> 1`, the 414/431 guards require `<= 0`; at exactly 1 neither fires → no read, no error, readable socket → spin (`ClientConnection.cpp:157`, `:376`, `:420`).

### Correctness — wrong status codes / broken config features

10. **Parse-error status clobber — the single biggest failing-test family (~14 failures).** `handle_setup()` unconditionally resets `_req.status = 200/201` (`ClientConnection.cpp:534-536`), discarding the 400/501/505/414 set by request-line/header parsing. Verified by the suite: `HTTP/2.0` → 200 (not 505), bogus versions/`HTTP/1.10` → 200, header-without-colon → 200, unknown methods (HEAD/PUT/OPTIONS/PATCH/XYZZY) → 405 (not 501), control-char URI → 302, bare method line → 405 (not 400), invalid %-escape → 200, `Content-Length: 123abc` → 200. One fix — don't reset a non-2xx status; route it to `epi_redirect()` — would clear the whole `error-response-test` + `parse-error-test` family. Subject: "your HTTP response status codes must be accurate."

11. **`conf/base.conf` duplicate `error_page 404` turns every 404 into `302 → google.com`.** The experiment line `error_page 404 =302 https://www.google.com;` (`base.conf:12`) wins because `config::errors::add_page` is last-wins (nginx keeps the first). Breaks `test-missing-file-is-error` and `test-missing-cgi-script-is-error` and masks status-clobber failures (400→404→302). Fix: drop the line and/or make duplicates first-wins.

12. **413 unreachable on non-CGI routes.** Method-allow runs before the body-size check (`ClientConnection.cpp:564-571`) and POST is rejected without `cgi_pass`, so an oversized POST to a static route gets 405, never 413. *(Failing test: `body-size-test/test-oversized-body-rejected`.)* Falls out of implementing upload (missing feature 1).

13. **Internal `return <code> <path>` always yields 500.** The `return` branch sets `_req.path` but never re-resolves `_loc` (`ClientConnection.cpp:548-563`), so the same location's `return` fires every loop iteration until `REDIRECT_LIMIT` forces a 500. Only external returns work. *(Failing test: `redirect-test/test-internal-return-redirects`.)*

14. **`index` replaces the whole path instead of appending.** `_req.path = _loc->get_index().path` (`ClientConnection.cpp:583`) makes `GET /subdir/` serve the *root* `/index.html`, not `/subdir/index.html`. *(Failing test: `static-index-test/test-subdirectory-index-is-served`.)*

15. **`client_header_buffer_size` is still dead — `set_capacity()` never assigns `capacity`.** The function allocates and copies but the `capacity = cap;` line only exists in `set_data()`, not `set_capacity()` (`ScratchBuffer.cpp:38-52`), so the 1024 default always wins. Its `std::min(sizeof(data), sizeof(new_data))` also copies 8 pointer-bytes, not buffer contents (currently harmless, pre-fill only). *(Failing test: `redirect-test/test-large-header-buffer-honored`.)*

16. **Header *values* lowercased wholesale.** The entire header line is lowercased before splitting (`ClientConnection.cpp:419`), corrupting case-sensitive values (multipart boundaries, cookies) — blocks CGI-based multipart upload. Lowercase only the key.

17. **Uninitialized members.** `Request::port` missing from the ctor init list (`Request.cpp:8-11`); `_cgi_pid` and `_written_body` missing from the `ClientConnection` ctor (`ClientConnection.cpp:108-114`).

18. **Destructor / fd bookkeeping.** A connection dying while `fd` is a CGI pipe never closes `_client_fd` (socket leak); `EpollLoop::delete_conn` (closes `conn->fd`) plus the destructor can double-close `_cgi_stdout_fd`.

19. **Oversized CGI header block → 502 instead of forwarded.** Policy decision pending (forwarding needs growable header buffering). *(Failing test: `cgi-robustness-test/test-oversized-cgi-header-is-forwarded` — expects forwarding.)*

20. **`Allow` header emitted on every internal response** — 200s and redirects carry `Allow: GET, POST, DELETE` (`ClientConnection.cpp:668`, `:987`); it belongs on 405 only.

21. **Duplicate request headers silently dropped** (FIXME in source).

### Compliance / housekeeping

22. **`fork()` used for autoindex.** The subject restricts fork to CGI; directory listing forks the external `./autoindex` helper. Likely evaluation flag — generate in-process with `opendir`/`readdir` (which *are* on the allowed list).

23. **`mkstemp` not on the allowed-functions list** (`ClientConnection.cpp:858`, `:975`) — replace with `open(O_CREAT|O_EXCL)` on a generated name. `bzero`/`strncpy` (`ScratchBuffer.cpp`, `Config.cpp:333`) clash with "prefer C++ versions".

24. **Untracked assets break fresh clones.** `www/` assets and test configs the suite depends on are not in git (`www/index.html` was already once lost to a DELETE test); `make test` on a fresh clone fails confusingly.

25. **5 ms `epoll_wait` tick** wakes the process ~200×/s even when idle — acceptable, but computing the nearest deadline (or a coarser tick) would be cleaner; also the failed-`epoll_wait` path prints and `break`s rather than handling `EINTR` (see issue 5).

26. **Dead code and stale artifacts.** `src/Connection.cpp` (conflicting second `ClientConnection` definition — ODR footgun), `src/CgiConnection.cpp` + `inc/CgiConnection.hpp`, `ServerBlock.cpp` stub, unused `AUTOI_*` enum values, dead `parse_cgi_headers()`, commented-out multi-accept loop. Makefile: `autoindex` target not `.PHONY` (stale helper after aindex changes), dead clang-detect `ifeq`, `$(ODIR)` should be order-only. Typo: `"HTTP Version Not SUpported"` (`Response.cpp:39`).

---

### Suggested priority

Half-close spin + `%25` loop first (remotely triggerable, timeout does **not** bound them, either alone is an evaluation-failing DoS) → `ScratchBuffer` feed wrap (crash) → parse-error status clobber (clears ~14 test failures, subject-mandated accuracy) → finish the timeout branch (stalls, `waitpid`, 408, body timeout) → file upload + chunked (subject features) → config-feature bugs (internal `return`, `index` append, header buffer size, Content-Type, base.conf 404 line) → compliance sweep (fork-for-autoindex, mkstemp, README).
