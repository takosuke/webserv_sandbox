#!/usr/bin/env python3
# Sets the CGI status via a lowercase "status:" header. RFC 3875 header names are
# case-insensitive, but the server compares key == "Status" case-sensitively, so
# it treats this as an ordinary header, never calls add_status_line, and the
# response goes out with no HTTP status line at all. Used by cgi_robustness_test.
import sys

body = "lc-status-body\n"
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("status: 200 OK\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
