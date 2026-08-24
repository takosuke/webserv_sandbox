#!/usr/bin/env python3
# Renders the full CGI environment as an HTML table, so `cgi_param` variables
# (APP_ENV, SECRET_KEY, ...) are visible next to the standard RFC 3875 set.
# Reads and echoes the request body when there is one.
import os
import sys

# The variables RFC 3875 says a server must provide, in the order an evaluator
# usually checks them. Anything else the server passed is listed after these.
RFC3875 = [
    "REQUEST_METHOD", "SERVER_PROTOCOL", "SERVER_SOFTWARE",
    "SERVER_NAME", "SERVER_PORT", "GATEWAY_INTERFACE",
    "SCRIPT_NAME", "SCRIPT_FILENAME", "PATH_INFO", "PATH_TRANSLATED",
    "QUERY_STRING", "CONTENT_TYPE", "CONTENT_LENGTH",
    "REMOTE_ADDR", "REMOTE_HOST",
]


def read_body():
    raw = os.environ.get("CONTENT_LENGTH", "")
    try:
        n = int(raw) if raw else 0
    except ValueError:
        n = 0
    return sys.stdin.read(n) if n > 0 else ""


def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace('"', "&quot;"))


def rows(names):
    out = []
    for name in names:
        value = os.environ.get(name)
        if value is None:
            out.append("<tr class='missing'><th>" + esc(name)
                       + "</th><td><em>not set</em></td></tr>")
        else:
            out.append("<tr><th>" + esc(name) + "</th><td>"
                       + esc(value) + "</td></tr>")
    return "\n".join(out)


body = read_body()
extra = sorted(k for k in os.environ if k not in RFC3875)

TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>CGI environment</title>
  <link rel="stylesheet" href="/static/css/style.css">
  <style>
    body { display: block; padding: 2rem; }
    .container { text-align: left; max-width: 60rem; margin: 0 auto; }
    table { border-collapse: collapse; width: 100%; margin-bottom: 2rem; }
    th, td { border-bottom: 1px solid #e4e4e4; padding: .35rem .6rem;
             font-family: monospace; font-size: .9rem; text-align: left;
             vertical-align: top; word-break: break-all; }
    th { width: 14rem; color: #222; }
    td { color: #555; }
    tr.missing td { color: #b00; }
    h2 { font-size: 1.1rem; margin: 1.5rem 0 .5rem; }
    pre { background: #f6f6f6; padding: .75rem; overflow-x: auto; }
  </style>
</head>
<body>
  <div class="container">
    <h1>CGI environment</h1>
    <p>Served by <code>{SCRIPT}</code> through <code>cgi_pass</code>.</p>

    <h2>RFC 3875 meta-variables</h2>
    <table>{RFC}</table>

    <h2>Everything else the server passed</h2>
    <p>The <code>cgi_param</code> directives from the config show up here.</p>
    <table>{EXTRA}</table>

    <h2>Request body ({BLEN} bytes)</h2>
    <pre>{BODY}</pre>

    <p><a href="/">back to the start page</a></p>
  </div>
</body>
</html>
"""

html = TEMPLATE
for placeholder, value in [
    ("{SCRIPT}", esc(os.environ.get("SCRIPT_NAME", "envdump.py"))),
    ("{RFC}", rows(RFC3875)),
    ("{EXTRA}", rows(extra) or "<tr><td><em>none</em></td></tr>"),
    ("{BLEN}", str(len(body))),
    ("{BODY}", esc(body) or "<em>empty</em>"),
]:
    html = html.replace(placeholder, value)

sys.stdout.write("Content-Type: text/html\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Length: " + str(len(html)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(html)
sys.stdout.flush()
