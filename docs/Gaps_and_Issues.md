# Webserv — Missing Features & Open Issues

*Snapshot: 2026-07-29, branch `bug-hunt-2` (`dad8583` + the uncommitted `ClientConnection.cpp` working-tree change). Test suite standing: `make test` → **96 tests, 127 assertions, 16 failures** (13 distinct tests). The pre-existing suite was 89/113/11 (9 distinct, from 12 at the 2026-07-21 snapshot); this snapshot adds 7 tests in three new namespaces pinning the defects found by hand, 4 of which fail.*

*Scope decisions confirmed with the evaluators: **chunked transfer-encoding is not required**, and **`mkstemp` is acceptable** despite not being on the subject's function list. Both are struck from the priority list below; the chunked test stays in the suite as documentation, not as a blocker.*

Cross-checked against `Architecture.md`, `subject.txt`, the current sources, a clean rebuild, and live probes against a running server. Items the older docs list that are **already fixed** and therefore not repeated: DELETE; binary files served intact; CGI env `CONTENT_LENGTH`/`CONTENT_TYPE`; the `handle_cgi_output` rewrite family; `/tmp/cgi_*` unlink; merge-conflict markers in `ServerConnection.cpp`; the half-close busy-loop; the `%25` decode loop; the `ScratchBuffer` signed→unsigned wrap; the parse-error status clobber; `base.conf`'s stray `error_page 404 → 302 google.com`; `index` replacing the path; `fork()` for autoindex.

Note on reading the test log: many test *names* still carry a `KNOWN-FAILING:` prefix baked into their description string. Only the lines marked `FAIL` are actually failing — several `KNOWN-FAILING`-named tests pass today.

---

## 0. Fixed since the 2026-07-21 snapshot

1. **408 on read timeout** (commit `2fb62b1`). `EpollLoop::run`'s sweep now calls `ClientConnection::handle_timeout()`, which — for any state before `DISCARD_BODY` — sets `_req.status = 408` and re-enters `handle_setup()` to build a real response. Verified live: a client that sends a partial header block gets `HTTP/1.0 408 Request Timeout` after the configured `client_header_timeout`. Clients that stop *reading* are still just cut (`del()`), which is acceptable. **But see open issue 8** — after `handle_setup()` runs, the configured timeout is silently replaced by the 60 s default.
2. **Static POST / file upload exists** (commits `50cba03`, `b064c58`, `23ff897`). `setup_post()` opens `root + path` with `out | app` and `handle_post_leftover()`/`handle_post()` stream the body into it; the response is `201 Created`. Verified live: `POST /newfile.txt` creates the file, a second POST appends. This closes the *feature* only in part — see missing feature 1 and open issues 1 and 2.
3. **413 is reachable on static routes** (commit `23ff897`). The body-size check is now front-loaded in `handle_setup()` before `setup_post()`, so an oversized POST no longer creates a file first. *Test now passing: `body-size-test/test-oversized-body-rejected`.*
4. **`Content-Type` is emitted** (commit `53e5c71`). `setup_res()` looks up the path's extension in `_loc->get_mime()` and adds the header. The mechanism works but the lookup key is wrong — see open issue 3; the header is present on every static response, always with the `default_type` value. *Test now passing: `content-type-test/test-cgi-without-status-defaults-to-200`. Still failing: `test-static-html-has-content-type`.*
5. **Test assets are tracked.** `www/` (30 files) and every `conf/*.conf` the suite needs are in git now; a fresh clone can run `make test`. (Was compliance issue 18.)
6. **The Makefile `obj/` race no longer reproduces.** `make fclean && make -j8` succeeded on repeated tries. The prerequisite is still an ordinary one (`$(NAME): $(ODIR) $(OBJS)`), so the race is latent — the order-only fix `$(ODIR)%.o: %.cpp | $(ODIR)` is still the correct form. (Was compliance issue 20.)

---

## 1. Missing features (subject requirements)

In order of severity:

1. **File upload is implemented but not usable as specified.** What works: a POST to a static location writes its body to `root + path` and answers 201. What is missing or wrong:
   - **Bodies larger than one buffer are silently truncated** (open issue 1) — the headline defect.
   - There is **no upload-storage-location directive**; the subject requires "storage location is provided". Uploads land wherever the URI points inside the location's `root`, so any POST is a write into the served document tree.
   - Semantics are append-only (`ios::app`). A repeated POST to the same URI grows the file instead of replacing it; there is no 409/204 distinction.
   - The 201 response echoes the *whole target file* back as the body (`setup_res` opens the same path for reading and sets `Content-Length` from `stat`).
2. ~~**Chunked transfer-encoding is not handled.**~~ **Out of scope** — confirmed with the evaluators. `grep -ri chunk src/ inc/` is still empty; only `Content-Length` bodies are read. `regression-test/test-chunked-post-unchunked-for-cgi` stays red on purpose as documentation of the gap.
3. **Timeout enforcement is still incomplete.** The 408 path exists (fix 1 above), but:
   - The configured timeout is lost after setup (open issue 8) — in practice every connection runs on the 60 s default from the moment the request line is parsed.
   - `client_body_timeout` is parsed and never read at runtime.
   - There is no CGI runtime timeout: an immortal CGI holds its connection forever, and `finalize_cgi`'s blocking `waitpid` (open issue 5) is unbounded.
4. **No built-in default error pages.** When no `error_page` matches, `epi_redirect()` produces a body-less status-only response. Verified live: `500` responses carry no body at all. The subject requires "default error pages if none are provided".
5. **No subject-compliant `README.md`.** The required shape (italic first line "*This project has been created as part of the 42 curriculum by …*", Description / Instructions / Resources incl. an AI-usage description) is absent; `README.md` is still internal design notes.

---

## 2. Open issues

In order of severity.

### Regressions and defects in the new POST/upload path

1. **Static uploads are truncated to the first buffer.** Verified live: `POST /up.txt` with `Content-Length: 5000` writes **959 bytes**, answers `201` early, and resets the connection. Cause: in `handle_post()`, the `_state == REQ_BODY` branch (`ClientConnection.cpp:1017-1037`) reads from the socket and then does `if (_buf.feed_capacity() > 0) { _state = RESPONSE; setup_res(); }` — it jumps to the response instead of feeding the buffer to `_stream`. The feeding half of `handle_post()` below that branch is only reachable in `CGI_TRANSMIT_BODY`, which a static POST never enters. Only the leftover bytes written by `handle_post_leftover()` during setup ever reach disk. *(Failing test: `upload-test/test-large-upload-is-not-truncated`, `conf/upload.conf` — a 5000-byte body lands as 945. `test-small-upload-reaches-disk` is the passing control.)*

2. **`limit_except` no longer applies to POST — 405 became 500 (new test failure).** `handle_setup()` now runs `if (_req.method == POST) { … } else while (…)` (`ClientConnection.cpp:580-658`), so a POST skips the whole resolution loop: no `is_method_allowed()`, no `is_file_existing()`, no `return`/`index`/`autoindex` handling, no directory check. Consequences:
   - `POST /readonly` on a `limit_except GET` location returns **500**, not 405. *(Newly failing test: `limit-except-test/test-post-blocked-on-restricted-location`.)*
   - `POST /` (a directory) returns 500 — `set_file` cannot open a directory for append. Verified live.
   - `return` and `error_page`-driven internal redirects do not fire for POST.
   - Path traversal is *not* a problem here: `normalize_req_path` collapses `..` before resolution, so `POST /../escaped.txt` stays inside the root (verified live).

3. **`Content-Type` is always the `default_type`.** `setup_res()` passes `_req.path.substr(ext_del)` — which **includes the dot** — to `config::mime::get_type()`, but the map is keyed without it (`add_type("html", "text/html")`, and `add_types()` stores the raw token from `types { text/html html; }`). Every lookup misses and falls back to `default_type`. Verified live: `GET /p.html` → `Content-Type: text/plain`. One-character fix: `substr(ext_del + 1)`. *(Failing test: `content-type-test/test-static-html-has-content-type`.)*

4. **Autoindex responses leak a temp file per request and carry no `Content-Type`.** `setup_autoindex()` generates the listing in-process (good) but writes it to `/tmp/autoindex_XXXXXX` and never unlinks it — unlike `finalize_cgi`, which removes its `/tmp/cgi_*`. Staging through a temp file is fine (`mkstemp` is cleared by the evaluators); leaking it is not. It also builds its headers by hand and never adds `Content-Type: text/html`. Both are fixed by unlinking right after reopening for read (the unlink-while-open idiom already used for CGI) and adding the header. *(Failing tests: `autoindex-test/test-autoindex-does-not-leak-temp-files` — 5 requests, 5 files left in `/tmp` — and `test-autoindex-has-html-content-type`, `conf/autoindex.conf`. `test-autoindex-lists-directory` is the passing control.)*

### Critical — crash / hang / DoS class (grade-0 risk)

5. **`finalize_cgi()` blocks the event loop in `waitpid(pid, NULL, 0)`** (`ClientConnection.cpp:1089`, plus the copy at `:925` in the 502 branch of `handle_cgi_output` — both marked FIXME). A CGI that closes stdout but keeps running stalls every other client. Needs `WNOHANG` + deferred reap, or `kill` then reap. *(Failing test: `cgi-robustness-test/test-slow-exiting-cgi-does-not-block-the-loop`.)*

6. **EpollLoop: use-after-del within a batch, and `EINTR` unhandled** (`EpollLoop.cpp:98-113`, still in-source TODOs). An event later in the same `epoll_wait` batch can dereference a connection already `del()`'d; a signal during `epoll_wait` `break`s the loop instead of continuing (the `if (ready < 0 && errno == EINTR) continue;` line is still commented out).

7. **`setup_cgi()` failure leaves the connection hung forever** (`ClientConnection.cpp:684-687`). On pipe/fork failure `handle_setup` sets `_req.status = 500` and `return (true)` without calling `setup_res()` or `mod()`ing to `EPOLLOUT`; the client waits indefinitely.

8. **The configured timeout is discarded after setup.** `handle_setup()` ends with `_timeout = _loc->get_header().timeout` (`ClientConnection.cpp:665`), but `config::header` is **not inherited** Server→Location: `Location::from_server()` copies `root`, `body`, `output`, `mime`, `errorpages`, `index`, `autoindex` — not `header` (`Config.cpp:1184-1192`), and `Location::operator=` omits it too. `client_header_timeout` is also rejected inside a `location` block by the parser, so a location's `header.timeout` is always the 60 s default. Net effect: the configured timeout only governs the request-line/header phase (set from the *server* in the ctor); from `handle_setup()` onward every connection silently reverts to 60 s, holding an fd and a half-written upload for a minute. *(Failing test: `timeout-test/test-stalled-body-times-out`, `conf/timeout_body.conf` — a client that stalls mid-body with `client_header_timeout 2` is still connected after 6 s. `test-stalled-headers-time-out` is the passing control, proving the value is honoured before setup. Neither test half-closes: an EOF is detected on its own and would mask the timeout.)*

9. **Zombie CGI processes on client abort.** No `kill()` exists anywhere in the compiled sources; if the client dies mid-POST the forked child is never killed or reaped — the destructor closes only pipe fds. Compounded by `_cgi_pid` being uninitialized (issue 15): `waitpid` on a garbage pid could reap an unrelated child.

10. **Accept-path fragility.** `set_nonblocking()` throws on `fcntl` failure and nothing between `conn->handle()` and `main()` catches it → process exit; `accept()` returning −1 on EMFILE leaves the listen fd EPOLLIN-ready → busy-spin until an fd frees (`ServerConnection.cpp:15-27`, `utils.cpp:10-16`).

11. **Off-by-one deadlock window at `fill_capacity() == 1`.** The read guards require `> 1`, the 414/431 guards require `<= 0`; at exactly 1 neither fires → no read, no error, readable socket → spin.

### Correctness — wrong status codes / broken config features

12. **Internal `return <code> <path>` always yields 500.** The `return` branch sets `_req.path` but never re-resolves `_loc`, so the same location's `return` fires every iteration until `REDIRECT_LIMIT` forces a 500. Only external returns work. *(Failing test: `redirect-test/test-internal-return-redirects`.)*

13. **`client_header_buffer_size` is still dead — `set_capacity()` never assigns `capacity`.** The function allocates and copies but the `capacity = cap;` line only exists in `set_data()` (`ScratchBuffer.cpp:38-52`), so the 1024 default always wins; a 3 KB request line under a 16 KB configured buffer still yields 414. Its `std::min(sizeof(data), sizeof(new_data))` also copies 8 pointer-bytes, not buffer contents (harmless, pre-fill only). *(Failing test: `redirect-test/test-large-header-buffer-honored`.)*

14. **Weak value validation → wrong status.** `Content-Length: 123abc` and the bogus minor version `HTTP/1.10` are both accepted instead of yielding 400: the version check uses `find_first_not_of('0', 8)`, and `Content-Length` is parsed with `istringstream >>` which stops at the junk without failing. A fully unparseable value returns 500, a client error reported as a server error. *(Failing tests: `parse-error-test/test-content-length-trailing-junk-is-400`, `test-bogus-minor-version-is-400`.)*

15. **Header *values* lowercased wholesale** (`ClientConnection.cpp:454`). The entire header line is lowercased before splitting, corrupting case-sensitive values (multipart boundaries, cookies) — blocks CGI-based multipart upload. Lowercase only the key.

16. **Uninitialized members.** `Request::port` is missing from the ctor init list (`Request.cpp`); `_cgi_pid`, `_written_body` and `_loc` are missing from the `ClientConnection` ctor init list (`ClientConnection.cpp:108-115`).

17. **Destructor / fd bookkeeping.** A connection dying while `fd` is a CGI pipe never closes `_client_fd` (socket leak); `EpollLoop::delete_conn` (closes `conn->fd`) plus the destructor can double-close `_cgi_stdout_fd`.

18. **Oversized CGI header block → 502 instead of forwarded.** Policy decision still pending (forwarding needs growable header buffering). *(Failing test: `cgi-robustness-test/test-oversized-cgi-header-is-forwarded`.)*

19. **`Allow` header emitted on every internal response** — 200s, 201s and redirects all carry `Allow: GET, POST, DELETE`; it belongs on 405 only. Verified live on every probe in this snapshot.

20. **Duplicate request headers silently dropped** (FIXME in source).

21. **`autoindex` takes `true`/`false`, not nginx's `on`/`off`.** `autoindex on;` is a hard config error (`[autoindex] only accepts 'true' and 'false' as parameter`). Harmless but surprising at evaluation, where a reviewer will type `on`; accepting both costs two lines.

### Compliance / housekeeping

22. ~~**`mkstemp` is not on the allowed-functions list**~~ — **cleared with the evaluators**; the CGI and autoindex temp files can keep using it. Unrelated and still open: `bzero`/`strncpy` (`ScratchBuffer.cpp`, `Config.cpp`) clash with "prefer C++ versions", and the autoindex temp file is still never unlinked (issue 4).

23. **5 ms `epoll_wait` tick** wakes the process ~200×/s even when idle — acceptable, but a nearest-deadline computation (or a coarser tick) would be cleaner.

24. **Dead code and stale artifacts.** `src/Connection.cpp` (conflicting second `ClientConnection` definition — ODR footgun), `src/CgiConnection.cpp` + `inc/CgiConnection.hpp`, `ServerBlock.cpp` stub, dead `parse_cgi_headers()` (declared and defined, never called), commented-out multi-accept loop. Stale build artifacts at the repo root: the `./autoindex` binary and `aindex/` (now that autoindex is compiled into `webserv`), plus untracked `minimal_serv.cpp`, `webserv_epoll.cpp`, `parser/`. Makefile: dead clang-detect `ifeq` (shell syntax inside `ifeq`, never true). Typo: `"HTTP Version Not SUpported"` (`Response.cpp:39`).

---

## 3. Current test failures at a glance

13 distinct tests, 16 assertions. The four marked **new** were added with this snapshot (`upload_test.clj`, `autoindex_test.clj`, `timeout_test.clj`, plus `conf/upload.conf`, `conf/autoindex.conf`, `conf/timeout_body.conf`); each ships with a passing control test next to it so a failure means "the defect", not "the feature is absent".

| Test | Symptom | Issue |
|---|---|---|
| `upload/test-large-upload-is-not-truncated` | 5000-byte body lands as 945 | 1 (**new**) |
| `limit-except/test-post-blocked-on-restricted-location` | 500, expected 405 | 2 (**new regression**) |
| `content-type/test-static-html-has-content-type` | `text/plain`, expected `text/html` | 3 |
| `autoindex/test-autoindex-does-not-leak-temp-files` | 5 requests → 5 files in `/tmp` | 4 (**new**) |
| `autoindex/test-autoindex-has-html-content-type` | header absent | 4 (**new**) |
| `timeout/test-stalled-body-times-out` | still connected after 6 s | 8 (**new**) |
| `cgi-robustness/test-slow-exiting-cgi-does-not-block-the-loop` | server unresponsive | 5 |
| `redirect/test-internal-return-redirects` | 500, expected 301 | 12 |
| `redirect/test-large-header-buffer-honored` | 414, expected 404 | 13 |
| `parse-error/test-content-length-trailing-junk-is-400` | 200, expected 400 | 14 |
| `parse-error/test-bogus-minor-version-is-400` | 200, expected 400 | 14 |
| `cgi-robustness/test-oversized-cgi-header-is-forwarded` | 502, expected forwarded | 18 |
| `regression/test-chunked-post-unchunked-for-cgi` | empty body | out of scope — chunked not required |

Still uncovered by any test: POST to a directory → 500 (issue 2, the same root cause as the `limit_except` failure), and the whole code-review-only class (issues 6, 7, 9, 10, 11, 16, 17).

---

## 4. Priority order

Ordered by *grade risk per unit of work*, not by how interesting the bug is. Two items are struck by the evaluators' rulings: **chunked encoding is out of scope**, and **`mkstemp` is allowed** (the autoindex *leak* is still a bug — the allowed-function objection is what's dropped).

### Phase 0 — one-liners that clear visible failures (an afternoon, at most)

| # | Item | Issue | Why now |
|---|---|---|---|
| 1 | `README.md` in the subject's format | feature 5 | Gates the evaluation before a single request is sent, and costs an hour. Nothing else on this list matters if the README is missing. |
| 2 | Mime lookup: `substr(ext_del + 1)` | 3 | One character. Restores every `Content-Type`, which is what makes a browser render CSS/JS/images — the "serve a fully static website" demo. Clears 1 failure. |
| 3 | Restore the resolution checks for POST | 2 | A *regression*: `limit_except` no longer blocks POST (500 instead of 405) and POST to a directory 500s. An evaluator types both. Clears 1 failure. |
| 4 | Autoindex: unlink the temp file, add `Content-Type: text/html` | 4 | Two lines, using the unlink-while-open idiom already in `finalize_cgi`. Clears 2 failures. |

### Phase 1 — broken subject features (the bulk of the grade)

| # | Item | Issue | Why |
|---|---|---|---|
| 5 | Stream the whole POST body to disk | 1 | "Clients must be able to upload files" is a subject line and the demo is *upload a file, then download it back*. Anything over ~1 KB is silently corrupted today. Clears 1 failure. |
| 6 | Upload storage location (an `upload_store`-style directive + routing) | feature 1 | The subject explicitly asks for the storage location to be configurable; today a POST writes into the served document tree at whatever path the URI names. |
| 7 | Built-in default error pages | feature 4 | Explicit subject requirement ("default error pages if none are provided"); today an unmatched code yields a body-less response. Self-contained: one static HTML template in `Response`. |
| 8 | Internal `return <code> <path>` | 12 | "HTTP redirection" is a required per-route rule. External returns work, internal ones 500. Clears 1 failure. |

### Phase 2 — hang / crash class ("must remain operational", grade-0 risks)

| # | Item | Issue | Why |
|---|---|---|---|
| 9 | `finalize_cgi` blocking `waitpid` → `WNOHANG` + deferred reap | 5 | One slow-exiting CGI stalls *every* client. Directly contradicts "must remain operational at all times". Clears 1 failure. |
| 10 | CGI runtime timeout (kill + reap a CGI that overruns) | feature 3 | The classic evaluation probe is an infinite-loop CGI. Pairs naturally with 9. |
| 11 | Stop discarding the configured timeout after setup | 8 | Either inherit `config::header` into `Location` or drop the reassignment in `handle_setup`. Also the place to wire `client_body_timeout`. Clears 1 failure. |
| 12 | `setup_cgi()` failure → build a 500 response instead of hanging | 7 | Three lines: call `setup_res()` on the failure path. |
| 13 | EpollLoop `EINTR` + use-after-del in a batch | 6 | Uncomment the `EINTR continue`; re-check `_connections.count(fd)` before dispatching. Both are latent crashes and both are already flagged in-source. |
| 14 | `kill()` + reap orphaned CGI children on client abort | 9 | Zombies accumulate for the process lifetime; `ps` during an evaluation is a fair question. |
| 15 | `accept`/`set_nonblocking` failure handling (EMFILE spin, thrown `fcntl`) | 10 | Under fd pressure the server either busy-spins or exits. |
| 16 | Uninitialised members, `fill_capacity() == 1` window, fd double-close | 11, 16, 17 | Cheap hygiene with real crash/hang tails; batch them into one pass. |

### Phase 3 — status-code accuracy and config fidelity

| # | Item | Issue | Why |
|---|---|---|---|
| 17 | Reject `Content-Length: 123abc` and `HTTP/1.10` with 400 | 14 | "Your HTTP response status codes must be accurate." Clears 2 failures. |
| 18 | Lowercase header *keys* only, not values | 15 | Corrupts multipart boundaries and cookies — blocks any CGI-based form upload. |
| 19 | `set_capacity()` must assign `capacity` | 13 | One line; makes `client_header_buffer_size` real. Clears 1 failure. |
| 20 | `Allow` only on 405 | 19 | Visible in every NGINX side-by-side comparison. |
| 21 | Accept `autoindex on\|off` as well as `true\|false` | 21 | Two lines; avoids a config-file surprise at the demo. |
| 22 | Duplicate request headers | 20 | Decide a policy (first-wins, like NGINX) and stop dropping silently. |

### Phase 4 — housekeeping (do last, or in idle moments)

Decide the oversized-CGI-header policy (issue 18 — keeping the 502 is defensible; if so, retire that test rather than leave it red), delete the dead code (`Connection.cpp`, `CgiConnection.*`, `ServerBlock.cpp`, `parse_cgi_headers`, the stale `./autoindex` binary and `aindex/`), replace `bzero`/`strncpy` with C++ forms, make `$(ODIR)` an order-only prerequisite, drop the dead clang `ifeq`, fix the `"Not SUpported"` typo, and consider computing the nearest deadline instead of the 5 ms tick.

**Out of scope:** chunked transfer-encoding (`regression-test/test-chunked-post-unchunked-for-cgi` stays red on purpose) and the `mkstemp` compliance objection.
