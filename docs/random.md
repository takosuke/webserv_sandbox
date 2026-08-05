

### no cgi runtime timeout:

- issue 5 finalize_cgi's blocking PID is unbounded: 

- **`finalize_cgi()` blocks the event loop in `waitpid(pid, NULL, 0)`** (`ClientConnection.cpp:1089`, plus the copy at `:925` in the 502 branch of `handle_cgi_output` — both marked FIXME). A CGI that closes stdout but keeps running stalls every other client. Needs `WNOHANG` + deferred reap, or `kill` then reap. *(Failing test: `cgi-robustness-test/test-slow-exiting-cgi-does-not-block-the-loop`.)



### EpollLoop use-after-del within a batch, EINTR unhandled:

- (`EpollLoop.cpp:98-113`, still in-source TODOs). An event later in the same `epoll_wait` batch can dereference a connection already `del()`'d; a signal during `epoll_wait` `break`s the loop instead of continuing (the `if (ready < 0 && errno == EINTR) continue;` line is still commented out).



### setup_cgi() failure leaves connection hung forever

- (`ClientConnection.cpp:684-687`). On pipe/fork failure `handle_setup` sets `_req.status = 500` and `return (true)` without calling `setup_res()` or `mod()`ing to `EPOLLOUT`; the client waits indefinitely.



### Zombie CGI processes on client abort

No `kill()` exists anywhere in the compiled sources; if the client dies mid-POST the forked child is never killed or reaped — the destructor closes only pipe fds. Compounded by `_cgi_pid` being uninitialized (issue 15): `waitpid` on a garbage pid could reap an unrelated child.



---





initializing _loc and _written_body  matters for:

- Issue 2 — restoring the resolution loop for POST rewires handle_setup's two _loc assignment branches, the ones that make _loc safe.
- Issue 12 — internal return needs _loc re-resolved mid-loop, adding a third assignment site.
- Issue 7 — setup_cgi() failure currently returns without setup_res(); fixing it adds a new exit path out of setup.
- Issue 1 — the truncated-upload fix is precisely a rewrite of the _written_body accounting in handle_post.

having a Request::port matters :

- If you're adding SERVER_PORT to the CGI env (the to-do already sitting at :795, right next to the SERVER_NAME you do set) — init to -1 now, since that's the consumer and -1 is already your "no port in Host" sentinel from :494.
- If you're not — delete the member. parse_req_headers still needs to validate the port substring to produce its 400, but it can drop the value. A field nothing reads is a question at evaluation you get nothing for answering.sho
- but add server port to cgi env like this:

`std::ostringstream port_oss;
port_oss << ntohs(_addr.sin_port);
env_strings.push_back("SERVER_PORT=" + port_oss.str());`

More to the point, it's the least valuable item in the very TODO comment it lives in (:794-796). If you're going to spend a pass on the CGI env, the one that actually changes what's possible is HTTP_*: right now a CGI cannot see any request header — no HTTP_COOKIE, no HTTP_USER_AGENT, no HTTP_ACCEPT. Sessions, auth, content negotiation, all impossible. That's a functional gap; SERVER_PORT is a correctness detail.

---



Two things worth knowing when you do that pass:

- REMOTE_ADDR needs plumbing, unlike SERVER_PORT. client_addr is filled by accept() and then discarded (ServerConnection.cpp:19-24) — the ClientConnection never receives it. You'd pass it through the ctor (or call getpeername, also allowed).
- PATH_INFO/SCRIPT_NAME are currently inverted. PATH_INFO gets the whole path (:803) and SCRIPT_NAME is unset. Per the RFC, SCRIPT_NAME is the path identifying the script and PATH_INFO is only the trailing remainder. Harmless for your Python scripts, which read neither, but it's the same category of fix and worth doing together.

My suggestion: leave the TODO where it is, and when you reach it, do the whole block in one pass — HTTP_* first, then SERVER_PORT, SCRIPT_NAME, SERVER_SOFTWARE, REMOTE_ADDR.



---



Two notes for when you pick the next item up: the Allow header (issue 19) shows on every response including these 502s, and the CGI FD_CLOEXEC gap we discussed is still open — D now bounds its damage to CGI_TIMEOUT + grace rather than forever, but the stdin-EOF hang between two concurrent CGI requests can still happen inside that window.



---





if (client_fd < 0) return; // TODO handle error?

This is already EINTR-correct by accident. You return without touching the connection, and because your epoll registration is level-triggered (EPOLLIN | EPOLLERR | EPOLLHUP, no EPOLLET at EpollLoop.cpp:58), the pending connection is still queued and the listen fd re-reports EPOLLIN on the next epoll_wait — 5 ms later you accept it. Nothing lost.

That TODO is still real, but it's the EMFILE problem from issue 10, not EINTR: on fd exhaustion the connection stays queued forever, the listen fd stays readable forever, and you spin at 200 wakeups/sec doing nothing. Different fix (accept-and-close a reserve fd, or EPOLL_CTL_DEL the listener until an fd frees), different patch.

---





Sites that need an explicit case-insensitive check added, because they currently rely on the blanket lowercase (removing it breaks them silently):

1. Config.cpp:699 (starts_with_scheme) — compares the URI prefix against a hardcoded lowercase list ("http", "https", …) with an exact match. Needs equals_icase (or fold a copy of the prefix) instead of literal compare, or https://EXAMPLE.com in a return directive stops being recognized as a scheme.
2. Config.cpp:1542-1546 (Port::get_server_by_name) — servername_map.find(name) is a plain std::map<std::string,...> lookup, case-sensitive by construction. This one's currently working only as a side effect of the bug: today _req.hostname reaches here already lowercased by the blanket transform, so it happens to match lowercase server_name entries. Once you fix line 454, this breaks for any client sending Host: EXAMPLE.com against a lowercase server_name example.com;. Fix at the call site, ClientConnection.cpp:568 — fold a local lowercase copy of _req.hostname before passing it into http->get_server(_addr, ...); don't mutate _req.hostname/_req.host themselves, since _req.hostname is forwarded to CGI as SERVER_NAME at ClientConnection.cpp:802 and should probably reflect what the client actually sent.

No fix needed right now — flagged only so you don't forget when you get there:

3. Content-Type media type: ClientConnection.cpp:811-814 only forwards the raw value to CGI, no server-side branching on it yet. When you do branch on it (e.g. detecting multipart/form-data), compare with equals_icase against just the type/subtype substring — leave anything after the ; (charset, boundary) alone.
4. Connection: no code reads this header at all currently (grep came up empty besides unrelated symbol names). Nothing to fix; when keep-alive is implemented, use equals_icase there too.

Already correct, no action:

5. ClientConnection.cpp:914 — equals_icase(key, "Status") inside handle_cgi_output, the live CGI-header-parsing path. Already does this right.

Unrelated cleanup, worth a look separately: ClientConnection.cpp:1057-1085, parse_cgi_headers() — declared in the header, defined, but never called anywhere (grep confirms zero call sites). It's a stale duplicate of the header-parsing logic that got superseded when handle_cgi_output was rewritten inline, and it still does key == "Status" (line 1072, case-sensitive, the old bug). Not live code so it can't bite you at runtime, but it'll confuse anyone reading the file next to the correct version — probably a delete candidate.

---



What this (_the try/catch in handle())_) not cover, worth being explicit about so it isn't oversold:

1. Not memory-safety bugs. A null-pointer dereference or stack overflow raises SIGSEGV, not a C++ exception — no try/catch intercepts a signal. This fix is specifically for "code correctly throws and nobody downstream catches it," which is what you've been finding (the out_of_range-shaped bugs, even though starts_with_scheme itself turned out not to be one).
2. Possible resource leaks on the unwind path. If an exception fires mid-setup — say after setup_cgi() has forked a child but before _cgi_pid is fully recorded — del(conn) → eventual delete conn → ~ClientConnection() needs to actually reap/close whatever was left dangling. Worth a quick look at the destructor once this is in, but it's a secondary concern, not a blocker for adding the catch itself.
3. CGI child processes are a separate address space — an exception can only originate in the parent's C++ code, never propagate from the forked interpreter, so this doesn't need to reason about CGI internals at all.

That's the whole design: one try/catch at the existing dispatch point, reusing the existing deferred-delete idiom, no new machinery. Want me to also point you to the destructor cleanup question (item 2), or leave that as a follow-up for later?

---



Three gaps, relevant specifically because the new catch-all means teardown can now happen at any point mid-request, not just at the tidy end-states the code was written assuming:

1. _client_fd is never closed, and can leak. It's set once at construction (_client_fd(sockfd), ClientConnection.cpp:110), but the base class's fd member — the one EpollLoop::delete_conn actually closes (close(conn->fd), EpollLoop.cpp:131) — gets reassigned away from the client socket during CGI phases via rearm() (EpollLoop.cpp:76, conn->fd = new_fd;), called at ClientConnection.cpp:873,878,937,990,998,1012,1015,1097 to point fd at _cgi_stdin_fd or _cgi_stdout_fd instead. So if an exception fires while a CGI request is in flight, fd equals one of the pipe fds — delete_conn closes that pipe, but the client socket is never fd at that moment and is never closed anywhere. Leaked per crashed CGI connection.

Compounding it: the destructor's unconditional close(_cgi_stdin_fd) / close(_cgi_stdout_fd) will then double-close whichever one delete_conn just closed as conn->fd a line earlier.

2. _cgi_pid is never reaped in the destructor. The only two waitpid(_cgi_pid, ...) calls (ClientConnection.cpp:928 and :1092) live deep inside the normal CGI-completion paths, not the destructor. Teardown before either runs — exactly what an early exception causes — leaves the forked child unreaped: a zombie that accumulates in the process table every time this fires.

3. The CGI temp file is never deleted, period — I grepped the whole tree, unlink() doesn't appear once in src/ or inc/. This isn't new or specific to the exception path; every CGI request already leaks a file under /tmp/cgi_XXXXXX today, exception or not. Flagging it because it's the same object (_file) that would matter for exception-time cleanup, but it's a pre-existing bug the catch-all doesn't create — worth its own fix separately.

(_stream itself needs nothing — std::fstream's destructor closes its own fd via RAII regardless of when teardown happens.)

So before the catch-all is safe to lean on, the destructor needs: close _client_fd unconditionally (drop the double-close risk by tracking which fd is "spare" vs. currently armed, or just always close all three and guard each with its own != -1 check plus setting fd = -1 after delete_conn closes it so the destructor doesn't re-close the same number), and reap _cgi_pid if it's still running (waitpid(_cgi_pid, NULL, WNOHANG) guarded by a "do we have a live child" flag, since -1 isn't a valid empty-sentinel for pid_t the way it is for int fds).
