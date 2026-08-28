# Manual verification scripts

Two fixed issues cannot be covered by the kaocha suite: both need the server to
run out of file descriptors, and `server/make-fixture` launches it through
`ProcessBuilder` with no way to set an rlimit. Fd-starvation timing is also
flaky enough that it does not belong in a suite that gates `make`.

These scripts are how those fixes were verified, and how a regression would be
caught. Run them from the repo root after `make` and `make prepare-confs`.

| Script | Issue | Checks |
|---|---|---|
| `emfile_spin.sh` | 10 | `accept()` hitting EMFILE does not busy-spin the event loop (was 99% CPU, now ~0%) |
| `setup_cgi_failure.sh` | 7 | A `setup_cgi()` failure answers 500 instead of hanging until the timeout |

Both print `PASS` / `FAIL` and exit non-zero on failure.

Two things to know if you edit them:

- The `ulimit` must be scoped to the **server subshell only**
  (`( ulimit -n 64; exec ./webserv ... ) &`). Leaking it into the Python client
  starves the client instead — the symptom looks identical and proves nothing.
- Issues 7 and 10 produce the *same* client-visible symptom at tight fd budgets.
  `setup_cgi_failure.sh` therefore reports whether the request reached the
  parser: only `parsed=yes` rows exercise issue 7; `parsed=no` rows never got
  past `accept()` and are issue 10's territory.
