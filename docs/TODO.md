*Updated 2026-07-29 (`bug-hunt-2`). Detail, evidence and a phased fix order for
every line: `docs/Gaps_and_Issues.md` (see §4 Priority order — start with the
README, the mime dot, the POST regression and the autoindex unlink).*

*Out of scope, confirmed with the evaluators: chunked transfer-encoding, and the
`mkstemp` allowed-functions objection.*

# FEATURES

- [~] File Uploads (non CGI post) — writes to disk and answers 201, but bodies
      over one buffer are truncated (only the setup leftover is written) and
      there is no upload-storage-location directive
- [ ] README.md in the subject's format
- [ ] built in error pages (no body at all when no `error_page` matches)
- [-] chunked transfer-encoding — not required

# ISSUES

### regressions (new this snapshot)

- [ ] POST skips the whole resolution loop: no limit_except (405 → 500), no
      file-existence check, no return/index/autoindex, 500 on a directory
- [ ] Content-Type always falls back to default_type — the extension lookup
      keeps the '.', the mime map is keyed without it (`substr(ext_del + 1)`)

### big ones

- [ ] cgi runtime timeout (check waitpid), finalize_cgi blocks event loop
- [ ] configured timeout is lost after handle_setup — `config::header` is not
      inherited Server→Location, so every connection reverts to the 60s default
      once the headers are parsed
- [x] EINTR unhandled — fixed 2026-08-05, see docs/past_issues/EINTR_unhandled.md
- [ ] use-after-del within a batch
- [ ] setup_cgi fail not handled
- [ ] if POST clients die halfway forked cgi process dont get killed
- [ ] header values lowercased, should only be the keys because cgi multipart
      upload boundaries are case sensitive
- [x] scratchbuffer signed/unsigned mixups
- [x] autoindex is redirecting subdir requests to root index
- [x] no content type on responses (header is emitted now — value still wrong,
      see regressions above)

### smaller issues

- [ ] autoindex leaks a /tmp/autoindex_* file per request (never unlinked) and
      sends no Content-Type — `autoindex_test.clj`
- [ ] internal `return <code> <path>` still collapses to 500
- [ ] client_header_buffer_size dead — set_capacity() never assigns capacity
- [ ] Content-Length trailing junk and HTTP/1.10 accepted (should be 400)
- [ ] Allow header sent on every response, belongs on 405 only
- [ ] autoindex takes true/false, not nginx's on/off
- [?] ClientConnection:414/431 if fill_capacity is exactly 1 it can hang
- [x] timeout not responding with 408 code (send-side only; receive-side still
      just cuts the connection)
- [x] 413 unreachable on non cgi routes

### CGI

- [ ] env incomplete
- [ ] oversized CGI header block answered with 502 instead of forwarded
- [x] timeouts (see above)

### housekeeping

- [x] www/ and conf/ assets tracked in git
- [-] mkstemp not on the allowed-functions list — cleared, it's allowed
- [ ] dead code: Connection.cpp, CgiConnection.{cpp,hpp}, ServerBlock.cpp,
      parse_cgi_headers(); stale ./autoindex binary + aindex/; untracked
      minimal_serv.cpp, webserv_epoll.cpp, parser/
- [ ] Makefile: `$(ODIR)` should be an order-only prerequisite (race no longer
      reproduces, but the form is still wrong); dead clang-detect ifeq
