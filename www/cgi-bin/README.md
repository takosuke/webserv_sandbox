# `www/cgi-bin/`

Two kinds of script live here. **Only the first group is safe to open during a
demo.**

## Demo scripts — well-behaved, always answer

| Script        | What it shows                                                     |
|---------------|-------------------------------------------------------------------|
| `greet.py`    | A plain 200 HTML response; reads `name` from a query string (GET) or a urlencoded body (POST). |
| `envdump.py`  | The full CGI environment as a table — this is where `cgi_param` variables show up. |
| `echo.py`     | `REQUEST_METHOD`, `QUERY_STRING` and the raw body, as text/plain.  |
| `upload.py`   | A file-upload form plus the handler that writes into `www/uploads/`. |
| `env.py`      | `CONTENT_LENGTH` / `CONTENT_TYPE` forwarding (used by the tests).  |
| `multi_header.py` | Several response headers forwarded from the script.           |
| `cgi_test`    | The 42 tester's binary, wired to the `\ .bla` location.            |

## Test probes — deliberately broken, do not open in a browser

These exist to make the server misbehave; the regression suite drives them on
purpose. Several never answer at all.

| Script              | Deliberate misbehaviour                                     |
|---------------------|-------------------------------------------------------------|
| `hello.py`          | Answers **400** on purpose (`greet.py` is the friendly one). |
| `hello_stdin.py`    | Same, echoing stdin.                                        |
| `hang_forever.py`   | Never writes, never exits — must be killed at `CGI_TIMEOUT`. |
| `sigterm_immune.py` | Ignores SIGTERM; only SIGKILL stops it.                     |
| `no_output.py`      | Exits without writing a byte.                               |
| `no_status.py`      | Omits the `Status:` line.                                   |
| `lc_status.py`      | Lowercase `status:` header.                                 |
| `lf_headers.py`     | Bare-LF header terminators — the response hangs.            |
| `big_header.py`     | A header value larger than the scratch buffer.              |
| `big_body.py`       | A body larger than the pipe buffer.                         |
| `fixed_body.py`     | Pins the exact first byte of the body.                      |
| `slow_exit.py`      | Answers, then lingers, to catch a blocking `waitpid`.       |

`webserv-tests/` refers to these by path, so they must stay in this directory.
