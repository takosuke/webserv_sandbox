# Webserv — Missing Features & Open Issues

*Snapshot: 2026-08-28. Test suite standing: `make test` → **103 tests, 145 assertions, 0 failures**. Previous snapshot (2026-08-05) was 102/143/14 (11 distinct failing tests).*

*Scope decisions confirmed with the evaluators: **chunked transfer-encoding is not required** (its test has been removed from the suite), and **`mkstemp` is acceptable** despite not being on the subject's function list.*

**Issue numbers are stable across snapshots** — fixed items are struck in §0 and their numbers retired, new findings get fresh numbers, so references in `Architecture.md`, `TODO.md` and `random.md` keep pointing at the right thing.

A green suite does **not** mean everything below is closed. Several open items are invisible to `make test` by nature (fd exhaustion, config startup paths); those are marked *script-verified* or *review-only* and say how they were checked.

---

## 0. Fixed since the 2026-08-05 snapshot

### Startup / config robustness — the whole grade-0 class is closed

1. **Issue 25 — a malformed, missing or unreadable config no longer crashes or hangs.** `Lexer::load` now checks `is_open()` *and* re-checks `fail() && !eof()` after the first `getline`, because a **directory opens successfully** (`is_open() == 1`) and only fails on read — an `is_open()` check alone, which the previous snapshot prescribed, would have left `./webserv conf` spinning. `Grouper` construction moved inside `main`'s try/catch, and `body_directives` is guarded for emptiness. All seven startup cases now exit 1 with a diagnostic:

    | Input | Result |
    |---|---|
    | `./webserv /nope/nothere.conf` | `rc=1` `cannot open config file …` |
    | `./webserv` (no argument) | `rc=1` `cannot open config file webserv.conf` |
    | `./webserv conf` (a directory) | `rc=1` `cannot read config file conf` |
    | empty file / no `http {}` | `rc=1` `config file has no http { } block` |
    | `this is not a config {{{ ;;;` | `rc=1` (caught `length_error`) |
    | `http { }` — no listener (**issue 33**) | `rc=1` `config defines no listening socket` |
    | `listen 127.0.0.1:99999` (**issue 33**) | `rc=1` `port out of range (1 - 65535)` |

    Previously rows 1–3 and 6–7 hung forever, row 4 segfaulted and row 5 aborted. *Script-verified; no suite coverage — a `config_robustness_test.clj` shelling out to `./webserv <bad file>` under a timeout would pin all seven cheaply.*

### Subject features completed

2. **Issue 27 — the CGI now receives the full request.** `HTTP_*` for every client header (uppercased, `-`→`_`, `Content-Length`/`Content-Type` excluded since they have their own `CONTENT_*`), plus `SERVER_PORT`, `REMOTE_ADDR` (via `getpeername`, since `_addr` holds the *listen* address), `SERVER_SOFTWARE`, `REDIRECT_STATUS`. `SCRIPT_NAME` now holds the script and `PATH_INFO` is empty — previously `PATH_INFO` held the script path, which actively misled any CGI that routed on it.
3. **Issue 28 — the CGI runs in the script's directory.** `chdir()` to the script's dirname before `execve`, with the basename as `argv[1]`.
4. **Feature 5 — built-in default error pages.** `Response::default_error_page()` generates an nginx-shaped HTML body from the status code and reason phrase, streamed via a new `buffer_inline_body()` that parallels `buffer_file()`. Fires only for `no_file && status >= 400`, so 204/3xx correctly stay body-less. The pre-existing `DEFAULT_ERROR_PAGE` / `errors::_default` scaffolding was dead (only ever read by `operator<<`) and pointed at a *file*, which would have made "built-in" pages depend on shipping a file into every document root.
5. **Feature 1 — upload is complete.** `upload_directory <dir> <location> [create_path]` routes uploads out of the served document tree. Semantics changed from append to **replace** (`ios::trunc`), matching nginx's DAV PUT ("first written to a temporary file, and then the file is renamed"); status is now **201 when the file is created, 204 when replaced**, per RFC 9110 §9.3.3 — previously a repeated POST grew the file without bound and still answered 201. The `Location` header was `host + "/" + …`, emitting a relative reference like `x/upload/f.txt`; it is now an absolute path. A precedence bug in `create_path` (`size_t d = find_last_of('/') != npos` assigns **1**, not the position) made subdirectory creation dead code.

### Hang / crash class

6. **Issue 7 — `setup_cgi()` failure no longer hangs the connection.** It set `status = 500` and returned without `setup_res()` or an `EPOLLOUT` rearm; since `_state` is already `RESPONSE` (8) and `handle_timeout`'s guard is `_state < DISCARD_BODY` (4), the connection was dropped 60 s later having sent **zero bytes**. Now calls `epi_redirect()` then `setup_res()`. The `epi_redirect()` is load-bearing: `no_file` is false on this path, so a bare `setup_res()` would take the file branch and serve **the CGI script's source** as the body of a 500. *Script-verified with `sweep.sh` (server under `ulimit -n 64`, 54–56 held connections): was a hang, now `HTTP/1.0 500`.*
7. **Issue 10 — the EMFILE accept spin is gone.** Under level-triggered epoll a failed `accept()` leaves the listen fd readable forever. Measured **99% CPU** (299 ticks/3 s) under fd starvation, now **0%**. Fixed with a reserve fd held by `EpollLoop` (`release_reserve_fd`/`reclaim_reserve_fd`) that is surrendered to accept-and-close the pending connection, draining the backlog entry so readiness clears; clients now get a clean close instead of a stall. The second half — `set_nonblocking`/`set_cloexec` throwing after `accept()` and leaking `client_fd` — is fixed with a try/catch that closes before rethrowing. *Script-verified with `spin.sh`.*
8. **Issue 17 — fd ownership is coherent.** `delete_conn` closed `conn->fd`, which `rearm()` had swapped to a **pipe** during CGI, and `~ClientConnection` then closed the same fd again. Ownership moved wholly into the destructors; `~ServerConnection` was added so the listen socket is still closed; `~EpollLoop`'s duplicate `close()` on shutdown was removed. **Correction to the previous snapshot: there was no fd leak.** Pre- and post-fix binaries measured identical (both return to baseline once `CGI_TIMEOUT` elapses); the earlier "leaks a socket per teardown" claim came from sampling fd counts *inside* the 10 s CGI timeout window and reading a bounded delay as a permanent leak. The real defect was the double-close.

### Status-code accuracy

9. **Issue 13 — `client_header_buffer_size` is live.** `set_capacity()` never assigned `capacity`, and its `std::min(sizeof(data), sizeof(new_data))` compared two *pointer* sizes, copying 8 bytes. This also silently fixed issue 18 (see §2).
10. **Issue 14 — `Content-Length: 123abc` and `HTTP/1.10` are 400.** The version check used `find_first_not_of('0', 8)`, which accepts trailing zeros; it is now a length check. `Content-Length` was parsed with `istringstream >>`, which stops at junk without setting `fail()`. The unparseable case also moved from 500 to 400 — a client error had been reported as a server error. *(Note: RFC 1945 and RFC 2616 both allow multi-digit versions via `1*DIGIT`; RFC 9112 §2.3 narrowed this to a single `DIGIT` each, and §2.2 mandates 400 for a request-line that doesn't match the grammar. Rejecting `HTTP/1.10` is a deliberate choice to follow the current spec.)*
11. **Issue 19 — `Allow` is emitted on 405 only.** It was coupled to `Location` in an `if/else` on `_req.internal`, so every internal response carried it. Now two independent conditions, which also closes a hole: RFC 9110 §15.5.6 makes `Allow` a **MUST** on 405, and an `else if` would have dropped it for a 405 that took an external `error_page` redirect.
12. **Issue 26 — `Host` matching is case-insensitive again.** A `to_lower()` helper folds the incoming `Host` at the lookup site *and* `server_name` values at parse time, so a config written `Static.Local` matches too. `_req.hostname` itself is left intact, since it is forwarded to the CGI as `SERVER_NAME`.
13. **Issue 29 — reason phrases for 411/414/431/504** added, plus the `"HTTP Version Not SUpported"` typo. An overlong URI used to go out as `414 Unknown Status Code`.
14. **Issue 32 — `Host` rules enforced.** HTTP/1.1 without `Host` → 400, and more than one `Host` → 400 (RFC 9112 §3.2), both version-gated so HTTP/1.0 is unaffected. The duplicate check reads `map::insert`'s existing `.second` return.

### Housekeeping

15. **Issue 24 — dead code removed.** `Connection.cpp`, `CgiConnection.{cpp,hpp}`, `ServerBlock.{cpp,hpp}`, `PidCollector.hpp` and `parse_cgi_headers()` are gone, as are the tracked `./autoindex` binary and `aindex/`. Nine merge-conflict markers left in `ClientConnection.cpp` by the `origin/upload-directive` merge (compiling only because they sat inside comment blocks) are gone.

---

## 1. Missing features (subject requirements)

1. **No subject-compliant `README.md`.** Still the internal design notes on response caching. Required shape: italic first line "*This project has been created as part of the 42 curriculum by …*", then Description / Instructions / Resources including an AI-usage description. **This gates the evaluation before a single request is sent.** Note that `./webserv` with no argument now exits 1 because the default path `webserv.conf` does not exist at the repo root — either document `./webserv conf/base.conf` or change the default.
2. **CGI is selected per-location via `cgi_pass`, not by file extension** (issue 30). The subject phrases it as "Execution of CGI, based on file extension (for example .php)". A `location /cgi-bin` block covers the demo, but a reviewer who writes a rule keyed on `.php` will not get one. Cheap middle ground: an optional extension list on `cgi_pass`, checked against the request path before forking.

Everything else in this section from previous snapshots — upload, CGI request completeness, CGI working directory, timeout enforcement, default error pages — is now done.

---

## 2. Open issues

### Config fidelity — latent, but one already bit

8 / 31. **`config::header` is not inherited Server→Location, and `Location::operator=` drops `cgi`, `header` and `output`.** `Location::from_server` (`Config.cpp:1280`) copies `root`, `body`, `output`, `mime`, `errorpages`, `index`, `autoindex`, `upload` — not `header`. `handle_setup` still ends with `_timeout = _loc->get_header().timeout`, so any path reaching that line reverts to the 60 s default.

  **The body timeout currently works by accident.** `timeout_body.conf`'s 2 s is honoured (probed: `408 Request Timeout` at 2.01 s) only because the static-POST branch returns *before* that reassignment. Any path that does reach it silently reverts. Fix by inheriting `header` in `from_server` and adding `cgi`/`header`/`output` to `operator=` — the copy ctor delegates to it and `copy_deep_container` copy-constructs, so today **any deep copy of a `Location` loses its `cgi_pass`**. Latent only because the parse path builds locations in place. *Review-verified.*

### Hang class — remaining

11. **Off-by-one window at `fill_capacity() == 1`.** Six read guards require `> 1` while the 414/431 guards require `<= 0`; at exactly 1 neither fires — no read, no error, readable socket. *Review-only; no reproducer yet.* Cheap hygiene with a real hang tail.

### Decided, not defects

18. **Oversized CGI header → 502 — resolved as policy, and the limit is configurable.** `buffer_res_headers` only places a header that fits the buffer, so under the 1024-byte default `big_header.py`'s ~3 KB header yields 502. This matches nginx, whose docs say of `fastcgi_buffer_size`: "if it exceeds the buffer size, the response is considered invalid." Issue 13's fix made the ceiling configurable — at `client_header_buffer_size 8192` the same header is **forwarded with a 200**. Both sides are now pinned by green tests (`cgi_robustness_test` on `base.conf`, `cgi_header_buffer_test` on `cgi_big_header.conf`). Nothing further to do.

20. **Duplicate request headers other than `Host` are first-wins.** `map::insert` keeps the first and discards the rest. This is now a **deliberate policy** matching nginx, not an oversight; only `Host` rejects duplicates, because only `Host` has an RFC MUST. Worth stating in `Configuration.md`.

21. **`autoindex` takes `true|false`, not nginx's `on|off` — deliberately.** The subject says only "You can take inspiration from the 'server' section of the NGINX configuration file" and explicitly permits "other rules or configuration information in your file"; its nginx-comparison lines are about *behaviour*, not syntax. The convention is documented in `Configuration.md`, and a wrong value produces a clear diagnostic (`[autoindex] only accepts 'true' and 'false' as parameter`, exit 1). **Not a defect.** If it is ever changed, `Configuration.md` must change in the same commit.

### Retired

6. ~~**EpollLoop use-after-del within a batch.**~~ **Demoted — this was overstated.** `del()` does not delete; it inserts into `_deletion_queue`, which `clear()` drains only *after* the event batch and the timeout sweep. The object stays alive and its fd stays open for the whole batch, and the queue is a `std::set`, so a double `del()` is idempotent. What remains is a connection that has already given up being dispatched again in the same batch — wasteful, not undefined behaviour. No dangling `this` exists on this path.

23. ~~**5 ms `epoll_wait` tick wakes the process ~200×/s.**~~ **Corrected — the cost is not measurable.** Idle CPU measures **0 ticks over 3 s**. The 99% CPU previously attributed partly to the tick was entirely issue 10's EMFILE spin. A nearest-deadline computation would still be tidier, but there is no performance argument for it.

### Compliance / housekeeping

22. **`bzero` / `strncpy` remain** at `ScratchBuffer.cpp:7,12,81` and `Config.cpp:334`, against "always prefer their C++ versions". `std::memset`/`std::memcpy` are drop-in.

24. **`tester` is a tracked 7 MB binary.** Keep it if it is the school's official tester, otherwise `git rm`. Also: `Makefile:66` has `$(NAME): $(ODIR) $(OBJS)` where `$(ODIR)` should be an order-only prerequisite on the object rule (`$(ODIR)%.o: %.cpp | $(ODIR)`). The dead clang-detect `ifeq` noted in earlier snapshots is already gone.

---

## 3. Test coverage at a glance

**103 tests, 145 assertions, 0 failures.**

Added or reworked this cycle:
- `cgi_header_buffer_test.clj` (new) — a large CGI header *is* forwarded when the buffer fits.
- `cgi_robustness_test` — the oversized-header test now asserts the 502 policy and the anti-spin guarantee, instead of asserting a forwarding behaviour that was never the intent.
- `upload_test` — retargeted at `upload_directory`; its docstring described the truncation bug as live long after it was fixed.
- `response_format_test` / `limit_except_test` — `Allow` coverage inverted and split: absent on 200, present on 405 listing `GET`. Previously the only assertion checked a place `Allow` does not belong.
- `regression_test` — the chunked test was removed (out of scope, not a pending gap).

**Not covered by any test**, and why:
- The whole config-robustness class (25, 33) — needs to shell out to `./webserv <bad file>` under a timeout. *Cheapest remaining coverage win; seven cases already scripted manually.*
- Issue 7 and issue 10 — need fd starvation, which `make-fixture` cannot set up (`ProcessBuilder` with no rlimit control) and which is inherently flaky. Verified by `sweep.sh` / `spin.sh`; keep those scripts.
- CGI env completeness (27) and working directory (28) — `www/cgi-bin/envdump.py` already renders the full env; a few assertions against it would be cheap.
- Default error pages (feature 5) — nothing asserts the generated body or that `Content-Length` matches the bytes sent. That mismatch was a real bug during implementation, so it is worth pinning.
- Case-insensitive `Host` (26), 411/414/431 reason phrases (29), issue 11.

---

## 4. Priority order

### Now

| # | Item | Issue | Why |
|---|---|---|---|
| 1 | `README.md` in the subject's format | feature 1 | Gates the evaluation before anything is run. Only item with no code component. |
| 2 | Inherit `header` in `Location::from_server`; add `cgi`/`header`/`output` to `operator=` | **8 / 31** | The configured timeout works by accident today. Two lines each, and it is the root cause of a bug already fixed once at the symptom level. |
| 3 | `config_robustness_test.clj` | 25, 33 | Seven cases already verified by hand; turning them into a test is mechanical and covers the largest untested surface. |

### Then

| # | Item | Issue | Why |
|---|---|---|---|
| 4 | Assertions on the default error page and CGI env | feature 5, 27 | Both are new, both are demo-visible, neither has coverage. |
| 5 | `fill_capacity() == 1` window | 11 | Cheap; the only remaining hang-class item. |
| 6 | CGI selection by file extension | **30** | The subject phrases the requirement that way. |

### Idle moments

`bzero`/`strncpy` → `std::memset`/`std::memcpy` (22); `git rm tester` and the Makefile order-only prerequisite (24); state the duplicate-header policy in `Configuration.md` (20).

**Out of scope:** chunked transfer-encoding (test removed), the `mkstemp` objection (cleared), `autoindex on|off` (21 — deliberate divergence), oversized CGI headers (18 — resolved as policy).
