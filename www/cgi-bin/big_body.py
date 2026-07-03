#!/usr/bin/env python3
# Emits a body far larger than the server's scratch buffer (default 1024) and
# larger than a pipe buffer (64 KB). handle_cgi_output does at most one fill()
# per event and then finalizes immediately on EPOLLHUP, so any bytes still
# sitting in the pipe when the child exits are lost -> the delivered body is
# shorter than advertised. Used by cgi_robustness_test's length check.
import sys

SIZE = 100000
body = "A" * SIZE
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
