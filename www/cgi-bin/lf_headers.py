#!/usr/bin/env python3
# Emits CGI header lines terminated with bare LF ('\n'), which RFC 3875 permits.
# The server splits CGI headers on CRLF only, so LF-terminated headers are never
# recognized as complete and the response hangs. Used by resilience_test.
import sys

body = "lf-headers-body\n"
sys.stdout.write("Content-Type: text/plain\n")
sys.stdout.write("Status: 200\n")
sys.stdout.write("\n")
sys.stdout.write(body)
sys.stdout.flush()
