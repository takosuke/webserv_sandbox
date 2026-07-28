# Webserv — Missing Features & Open Issues

*Snapshot: 2026-07-21, `main` (aa52399). Test suite standing: `make test` → **89 tests, 113 assertions, 12 failures** (9 distinct tests), down from 32 at the 2026-07-15 snapshot.*

Cross-checked against `Architecture.md`, `subject.txt`, the current sources, and a live rebuilt run. Items Architecture.md lists that are **already fixed** and therefore *not* repeated in the open-issue list: DELETE works; binary files serve intact; CGI env `CONTENT_LENGTH`/`CONTENT_TYPE`; the whole `handle_cgi_output` rewrite family (LF headers, case-insensitive `Status:`, blank-line consumption, EOF-driven finalize, 502 on dead-before-headers); `/tmp/cgi_*` unlink; merge-conflict markers in `ServerConnection.cpp` (gone). A **partial** timeout mechanism exists (`EpollLoop::run` ticks at 5 ms and sweeps `_last_update`/`_timeout`; Architecture.md's "epoll_wait blocks with timeout `-1`" is stale). The timeout branch is known-incomplete; its gaps are itemized below.

---

## 0. Fixed since the 2026-07-15 snapshot

These seven items from the previous list are now resolved and confirmed by a clean rebuild + `make test` (they moved the count from 32 → 12 failures). They are **not** repeated in the open-issue sections.

1. **Half-close / incomplete-request busy-loop (was Critical #1).** `handle()` now detects `readret == 0` (peer EOF) and `del()`s the connection in both the REQ_LINE/REQ_HEADERS path and the `DISCARD_BODY` path (`ClientConnection.cpp:141,156,165,166`). Commit `eef5fe0`. *Tests now passing: `resilience-test/test-half-closed-incomplete-request-does-not-hang`, `test-oversized-body-half-close-does-not-hang`, `test-cgi-lf-only-headers-do-not-hang`.*
2. **`decode_http()` infinite loop on a literal `%` + high-bit UB (was Critical #2).** Safety/bounds checks added to `decode_http`/`decode_hex` (commits `675a40e`, `d7f28bd`). *Tests now passing: `resilience-test/test-percent-encoded-percent-does-not-loop`, `test-double-encoded-percent-does-not-loop`, `test-high-bit-percent-escape-stays-responsive`, `parse-error-test/test-invalid-percent-encoding-is-400`.*
3. **`ScratchBuffer` signed→unsigned wrap on failed `read`/`write` (was Critical #3).** `fill(int fd)` and `feed(int fd)` now use `int readret`/`int writeret` so a `-1` return no longer wraps `size_t` (`ScratchBuffer.cpp:64-70,89-93`). Commit `aa52399`.
4. **Parse-error status clobber (was Correctness #10, the biggest failing family).** `handle_setup()` now guards `if (_req.status != 0)` and routes the preserved 400/501/505/414 to `epi_redirect()` instead of resetting to 200/201 (`ClientConnection.cpp:557`). Commit `fc39813`. *Tests now passing: the entire `error-response-test` group (`HTTP/2.0`→505, garbage/overlong version→400, unknown methods & PUT/HEAD/OPTIONS/PATCH→501, bare method line, empty/garbage request), plus `parse-error-test/test-control-char-in-uri-is-400`, `test-bare-method-line-is-400`, and `regression-test/test-header-without-colon-rejected`.*
5. **`conf/base.conf` duplicate `error_page 404`→`302 google.com` (was Correctness #11).** The experiment line was removed; `base.conf` now has a clean `error_page 404 /404.html`. Commit `95ebf49`. *Tests now passing: `static-files-test/test-missing-file-is-error`, `cgi-test/test-missing-cgi-script-is-error`.*
6. **`index` replaced the whole path (was Correctness #14).** It now appends: `_req.path += _loc->get_index().path` (`ClientConnection.cpp:609`), and `index`/`autoindex` are now inherited Http→Server→Location. Commit `aa52399`. *Test now passing: `static-index-test/test-subdirectory-index-is-served`.*
7. **`fork()` for autoindex (was Compliance #22).** Directory listing is now generated in-process via `autoindex::as_html()` (`opendir`/`readdir`, compiled from `src/autoindex.cpp` + `src/autoindex_File.cpp` into the main binary). The only remaining `fork()` in `src/` is the CGI fork (`ClientConnection.cpp:780`). Commit `aa52399`. *(Note: `setup_autoindex` still writes to a `/tmp/autoindex_XXXXXX` temp file via `mkstemp` — see Compliance #23, now applicable to autoindex too.)*

---

## 1. Missing features (subject requirements)

In order of severity:

1. **File upload (non-CGI POST) is unimplemented.** `is_method_allowed()` (`ClientConnection.cpp:483-488`) rejects POST on any location without `cgi_pass`; there is no upload-storage-location config and no code path that writes a body to disk. Subject: "Clients must be able to upload files" + "storage location is provided". Side effect: `client_max_body_size`/413 is unreachable on static routes (see open-issue 7).

2. **Chunked transfer-encoding is not handled.** No un-chunking code exists anywhere (`grep -ri chunk src/ inc/` is empty); only `Content-Length` bodies are read. Subject explicitly requires un-chunking chunked requests before handing the body to CGI. *(Failing test: `regression-test/test-chunked-post-unchunked-for-cgi`.)*

3. **Timeout enforcement is incomplete** (timeout branch merged but unfinished). What exists: a 5 ms `epoll_wait` tick and a per-connection sweep in `EpollLoop::run()` (`EpollLoop.cpp:117`) against `_timeout` (default 60 s, from `client_header_timeout`). The two readable-EOF spins that the timeout could *not* previously bound (half-close, `%25` loop) are now closed at the source (see section 0, fixes 1–2), so the timeout is no longer the last line of defence against them. What's still missing:
   - Timed-out connections are silently closed — no `408 Request Timeout` response.
   - `client_body_timeout` is parsed but never read at runtime (only `get_header().timeout` is used).
   - No CGI runtime timeout: an immortal CGI holds its connection forever, and `finalize_cgi`'s blocking `waitpid` (open-issue 1) is unbounded.
   - Subject: "A request to your server should never hang indefinitely."

4. **No `Content-Type` on any response; `types` / `default_type` are parsed-but-unused.** `setup_res` emits `Allow`/`Date`/`Content-Length` but never a MIME type, so "serve a fully static website" is only partially met (browsers sniff HTML but mishandle CSS/JS/images). *(Failing tests: `content-type-test/*`.)*

5. **No subject-compliant `README.md`.** The required format (italic first line "*This project has been created as part of the 42 curriculum by …*", Description / Instructions / Resources incl. AI-usage sections) is absent; the current `README.md` is internal design notes.

6. **No built-in default error pages.** The subject requires "default error pages if none are provided"; when no `error_page` matches, `epi_redirect()` produces a body-less status-only response rather than a default HTML page.

---

## 2. Open issues

In order of severity.

### Critical — crash / hang / DoS class (grade-0 risk)

*(The three previous top items — half-close busy-loop, `%25` decode loop, `ScratchBuffer` `read`/`write` wrap — are now fixed; see section 0. Line numbers below are approximate after the recent edits — re-grep before editing.)*

1. **`finalize_cgi()` blocks the event loop in `waitpid(pid, NULL, 0)`** (in `finalize_cgi`, and the copy in the 502 branch of `handle_cgi_output` — both marked FIXME). A CGI that closes stdout but keeps running stalls every other client until it exits. Needs `WNOHANG` + deferred reap, or `kill` then reap. *(Failing test: `cgi-robustness-test/test-slow-exiting-cgi-does-not-block-the-loop`.)*

2. **EpollLoop: use-after-del within a batch, and `EINTR` unhandled.** Both still open as in-source TODOs (`EpollLoop.cpp:98-106`): an event later in the same `epoll_wait` batch can dereference a connection already `del()`'d, and a signal during `epoll_wait` `break`s the loop instead of continuing (the `if (ready < 0 && errno == EINTR) continue;` line is commented out).

3. **`setup_cgi()` failure leaves the connection hung forever.** On pipe/fork failure `handle_setup` sets `_req.status = 500` and `return (true)` without ever calling `setup_res()` or `mod()`ing to EPOLLOUT; the client waits indefinitely.

4. **Zombie CGI processes on client abort.** No `kill()` exists anywhere in `src/`; if the client dies mid-POST, the forked child is never killed or reaped — the destructor closes only pipe fds. Zombies accumulate for the server's lifetime. Compounded by `_cgi_pid` being uninitialized (issue 12): `waitpid` on a garbage pid could reap an unrelated child.

5. **Accept-path fragility.** `set_nonblocking()` throws on `fcntl` failure and nothing between `conn->handle()` and `main()` catches it → process exit; `accept()` returning −1 on EMFILE leaves the listen fd EPOLLIN-ready → busy-spin until an fd frees (`ServerConnection.cpp:20-27`, `utils.cpp:10-16`).

6. **Off-by-one deadlock window at `fill_capacity() == 1`.** The read guard requires `> 1`, the 414/431 guards require `<= 0`; at exactly 1 neither fires → no read, no error, readable socket → spin.

### Correctness — wrong status codes / broken config features

*(The parse-error status clobber, the base.conf 404→302 line, and the `index`-replaces-path bug are now fixed; see section 0.)*

7. **413 unreachable on non-CGI routes.** Method-allow runs before the body-size check and POST is rejected without `cgi_pass`, so an oversized POST to a static route gets 405, never 413. *(Failing test: `body-size-test/test-oversized-body-rejected` — currently returns 405.)* Falls out of implementing upload (missing feature 1).

8. **Internal `return <code> <path>` always yields 500.** The `return` branch sets `_req.path` but never re-resolves `_loc`, so the same location's `return` fires every loop iteration until `REDIRECT_LIMIT` forces a 500. Only external returns work. *(Failing test: `redirect-test/test-internal-return-redirects`.)*

9. **`client_header_buffer_size` is still dead — `set_capacity()` never assigns `capacity`.** The function allocates and copies but the `capacity = cap;` line only exists in `set_data()`, not `set_capacity()` (`ScratchBuffer.cpp:38-52`), so the 1024 default always wins; a 3 KB request line under a 16 KB configured buffer still yields 414. Its `std::min(sizeof(data), sizeof(new_data))` also copies 8 pointer-bytes, not buffer contents (currently harmless, pre-fill only). *(Failing test: `redirect-test/test-large-header-buffer-honored`.)*

10. **Weak value validation → wrong status.** `Content-Length: 123abc` (trailing junk) and the bogus minor version `HTTP/1.10` are both accepted instead of yielding 400: the version check uses `find_first_not_of('0', 8)` which tolerates `HTTP/1.10`/`1.100`, and `Content-Length` parsing accepts trailing non-digits. *(Failing tests: `parse-error-test/test-content-length-trailing-junk-is-400`, `test-bogus-minor-version-is-400`.)*

11. **Header *values* lowercased wholesale.** The entire header line is lowercased before splitting, corrupting case-sensitive values (multipart boundaries, cookies) — blocks CGI-based multipart upload. Lowercase only the key.

12. **Uninitialized members.** `Request::port` missing from the ctor init list (`Request.cpp`); `_cgi_pid` and `_written_body` missing from the `ClientConnection` ctor.

13. **Destructor / fd bookkeeping.** A connection dying while `fd` is a CGI pipe never closes `_client_fd` (socket leak); `EpollLoop::delete_conn` (closes `conn->fd`) plus the destructor can double-close `_cgi_stdout_fd`.

14. **Oversized CGI header block → 502 instead of forwarded.** Policy decision pending (forwarding needs growable header buffering). *(Failing test: `cgi-robustness-test/test-oversized-cgi-header-is-forwarded` — expects forwarding.)*

15. **`Allow` header emitted on every internal response** — 200s and redirects carry `Allow: GET, POST, DELETE`; it belongs on 405 only.

16. **Duplicate request headers silently dropped** (FIXME in source).

### Compliance / housekeeping

17. **`mkstemp` not on the allowed-functions list** — used for the CGI temp file *and now the autoindex temp file* (`setup_autoindex`'s `/tmp/autoindex_XXXXXX`). Replace with `open(O_CREAT|O_EXCL)` on a generated name. `bzero`/`strncpy` (`ScratchBuffer.cpp`, `Config.cpp`) clash with "prefer C++ versions".

18. **Untracked assets break fresh clones.** `www/` assets and test configs the suite depends on are not in git (`www/index.html` was already once lost to a DELETE test); `make test` on a fresh clone fails confusingly.

19. **5 ms `epoll_wait` tick** wakes the process ~200×/s even when idle — acceptable, but computing the nearest deadline (or a coarser tick) would be cleaner; also the failed-`epoll_wait` path prints and `break`s rather than handling `EINTR` (see issue 2).

20. **Makefile `obj/` prerequisite is a footgun — re-confirmed live this snapshot.** `make re` on a clean tree fails (`fatal error: opening dependency file obj/*.d: No such file or directory`) because `$(ODIR)` is an ordinary prerequisite, not order-only, and the parallel build races the directory creation. Workaround used this run: `mkdir -p obj && make -j1`. Fix: `$(ODIR)%.o: %.cpp | $(ODIR)`.

21. **Dead code and stale artifacts.** `src/Connection.cpp` (conflicting second `ClientConnection` definition — ODR footgun), `src/CgiConnection.cpp` + `inc/CgiConnection.hpp`, `ServerBlock.cpp` stub, unused `AUTOI_*` enum values, dead `parse_cgi_headers()`, commented-out multi-accept loop. Makefile: `autoindex` target not `.PHONY`, dead clang-detect `ifeq`. Typo: `"HTTP Version Not SUpported"` (`Response.cpp:39`).

---

### Suggested priority

`finalize_cgi` blocking `waitpid` + finish the timeout branch (408, body timeout, CGI runtime timeout — bounds the last remaining hang class) → file upload + chunked (subject features; upload also unblocks 413 on static routes, issue 7) → config-feature bugs (internal `return`, header buffer size, Content-Type, weak `Content-Length`/version validation) → resilience hardening (EpollLoop EINTR/use-after-del, setup_cgi hang, zombie CGI, accept fragility) → compliance sweep (mkstemp, Makefile `obj/`, README, dead code).
