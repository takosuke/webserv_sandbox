#!/usr/bin/env python3
# Emits one header whose value is far larger than the scratch buffer (default
# 1024). buffer_res_headers() only copies a header when fill_capacity() exceeds
# its size, so an oversized header is never placed; handle_response then writes
# 0 bytes and closes the connection mid-response. A compliant server (and nginx)
# forwards large headers fine. Used by cgi_robustness_test's big-header probe.
import sys

marker = "BIGHEADERVALUE" + ("Z" * 3000)
body = "big-header-body\n"
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("X-Big: " + marker + "\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
