#!/usr/bin/env python3
# Emits headers WITHOUT a Status: line and no explicit status. A compliant
# server must default the response to 200 and still produce an "HTTP/x 200"
# status line. Used by content_type_test to catch the missing-status-line bug.
import sys

body = "no-status-body\n"
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
