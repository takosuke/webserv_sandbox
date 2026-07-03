#!/usr/bin/env python3
# Emits a body with a known, non-CRLF first byte. The server stops CGI header
# parsing at the blank line but never erases those 2 bytes, so the "\r\n"
# separator leaks into the body temp file and every CGI body is prefixed with an
# extra CRLF. Used by cgi_robustness_test's exact-prefix check.
import sys

body = "FIXEDBODYSTART-and-more\n"
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
