*Updated 2026-08-28. Detail, evidence and a phased fix order for every line:
`docs/Gaps_and_Issues.md` (see §4 Priority order — start with the README and the
`config::header` inheritance).*

*Suite standing: `make test` → **103 tests, 145 assertions, 0 failures**.
A green suite does not mean the list below is empty — fd exhaustion and config
startup paths are not reachable from kaocha and are verified by `sweep.sh` /
`spin.sh` instead.*

*Out of scope, confirmed with the evaluators: chunked transfer-encoding (its
test has been removed) and the `mkstemp` allowed-functions objection.*

# FEATURES

- [x] File uploads (non-CGI POST) — full-length writes, `upload_directory`
      storage location, replace semantics with 201 on create / 204 on replace
- [x] Built-in error pages — generated in-process, no filesystem dependency
- [x] CGI receives the full request (`HTTP_*`, `SERVER_PORT`, `REMOTE_ADDR`,
      `SCRIPT_NAME`, `SERVER_SOFTWARE`) and runs in the script's directory
- [ ] README.md in the subject's format — **gates the evaluation**; note
      `./webserv` with no argument now exits 1, so document
      `./webserv conf/base.conf` or change the default path
- [ ] CGI selection by file extension (`.php`-style rules) — issue 30
- [-] chunked transfer-encoding — not required

# ISSUES

### open

- [ ] `config::header` not inherited Server→Location, and `Location::operator=`
      drops `cgi`/`header`/`output` — issues 8/31. The configured body timeout
      only works because the static-POST branch returns before the
      `_timeout = _loc->get_header().timeout` reassignment. Any deep copy of a
      `Location` also loses its `cgi_pass`.
- [ ] `ClientConnection` hang window when `fill_capacity()` is exactly 1 —
      issue 11; six read guards use `> 1`, the 414/431 guards use `<= 0`
- [ ] `bzero`/`strncpy` should be `std::memset`/`std::memcpy` — issue 22
- [ ] `tester` is a tracked 7 MB binary; `$(ODIR)` should be an order-only
      prerequisite — issue 24

### decided — not defects

- [-] `autoindex` takes `true|false`, not `on|off` — deliberate. The subject
      only says "take inspiration from" nginx and permits other rules; the
      convention is documented and a wrong value gives a clear error. If this
      ever changes, `Configuration.md` changes in the same commit.
- [-] Duplicate request headers are first-wins (nginx behaviour); only `Host`
      rejects duplicates, because only `Host` has an RFC MUST — issue 20
- [-] Oversized CGI header block → 502 — matches nginx, and the limit is
      `client_header_buffer_size`, so a big enough buffer forwards it. Both
      sides pinned by green tests — issue 18

### retired — previous snapshots were wrong

- [-] EpollLoop use-after-del in a batch (issue 6) — `del()` defers into
      `_deletion_queue`, drained by `clear()` after the batch. No dangling
      pointer exists; at worst a connection that gave up is dispatched again.
- [-] fd leak on mid-CGI teardown (issue 17) — there was no leak. Pre- and
      post-fix binaries measure identically; the earlier claim came from
      sampling inside the 10 s `CGI_TIMEOUT` window. The real bug was a
      double-close, now fixed.
- [-] 5 ms `epoll_wait` tick cost (issue 23) — idle CPU measures 0 ticks over
      3 s. The 99% CPU was entirely the EMFILE spin.

### closed this cycle

- [x] Config robustness: missing/unreadable/empty/garbage file, no listener,
      out-of-range port — all seven exit 1 with a diagnostic (issues 25, 33)
- [x] `setup_cgi()` failure now answers 500 instead of hanging (issue 7)
- [x] EMFILE accept spin: 99% CPU → 0%, plus the fd leak on throw (issue 10)
- [x] fd ownership moved into the destructors, double-close removed (issue 17)
- [x] `Allow` on 405 only (issue 19)
- [x] HTTP/1.1 without `Host`, and duplicate `Host`, return 400 (issue 32)
- [x] `Content-Length` trailing junk and `HTTP/1.10` return 400 (issue 14)
- [x] `client_header_buffer_size` is live — `set_capacity()` assigns (issue 13)
- [x] Case-insensitive `Host` matching restored (issue 26)
- [x] Reason phrases for 411/414/431/504, `SUpported` typo (issue 29)
- [x] Dead code and merge-conflict markers removed (issue 24)

# TEST COVERAGE GAPS

Everything below passes by hand but has no test:

- [ ] config robustness — shell out to `./webserv <bad file>` under a timeout;
      seven cases already scripted. **Cheapest remaining win.**
- [ ] default error page body and `Content-Length` agreement — the mismatch was
      a real bug during implementation
- [ ] CGI env completeness and working directory — `www/cgi-bin/envdump.py`
      already renders everything needed
- [ ] case-insensitive `Host`, 411/414/431 reason phrases
- [-] issues 7 and 10 — need fd starvation; keep `sweep.sh` / `spin.sh` instead,
      kaocha cannot set an rlimit on the server process
