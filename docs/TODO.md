- [ ] File Uploads (non CGI post)
- [ ] README.md
- [ ] built in error pages

# ISSUES

### big ones

- [ ] cgi runtime timeout (check waitpid), finalize_cgi blocks event loop
- [ ] no content type on responses
- [ ] scratchbuffer signed/unsigned mixups
- [ ] autoindex is redirecting subdir requests to root index (GET idxdir/ returns /index.html not idxdir/index.html)
- [ ] EINTR unhandled, use-after-del within a batch
- [ ] setup_cgi fail not handled
- [ ] if POST clients die halfway forked cgi process dont get killed
- [ ] header values lowercased, should only be the keys because cgi multipart upload boundaries are case sensitive
### smaller issues
- [ ] timeout not responding with 408 code
- [ ] ClientConnection:414/431 if fill_capacity is exactly 1 it can hang
- [ ] 413 unreachable on non cgi routes

### CGI
- [ ] env incomplete
- [ ] timeouts (see above)