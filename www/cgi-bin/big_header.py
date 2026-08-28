#!/usr/bin/env python3
# Emits one header whose value (~3 KB) is far larger than the default 1024-byte
# client_header_buffer_size. buffer_res_headers() only places a header when
# fill_capacity() exceeds its size, so under that default it never fits and the
# request is answered 502 — the same call nginx makes, which treats an upstream
# header past its buffer as an invalid response. The limit is configurable, so
# with a big enough buffer the identical header is forwarded intact. Both sides
# are pinned: cgi_robustness_test (base.conf, 1024) and cgi_header_buffer_test
# (cgi_big_header.conf, 8192).
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
