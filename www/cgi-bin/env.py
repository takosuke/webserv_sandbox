#!/usr/bin/env python3
# Dumps the CGI environment variables the tests care about, plus the request
# body. Used by the regression tests to check CONTENT_LENGTH / CONTENT_TYPE
# forwarding (see webserv-tests/.../regression_test.clj).
import os
import sys

clen = os.environ.get('CONTENT_LENGTH', '')
ctype = os.environ.get('CONTENT_TYPE', '')

n = 0
try:
    n = int(clen) if clen else 0
except ValueError:
    n = 0
body = sys.stdin.read(n) if n > 0 else ''

out = ("CONTENT_LENGTH=" + clen + "\n"
       + "CONTENT_TYPE=" + ctype + "\n"
       + "BODY=" + body + "\n")

sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Length: " + str(len(out)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(out)
sys.stdout.flush()
