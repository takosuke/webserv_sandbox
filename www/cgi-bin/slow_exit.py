#!/usr/bin/env python3
# Writes a complete CGI response, flushes and CLOSES stdout (so the server sees
# the pipe hang up and can build the response), then keeps the process alive for
# a couple of seconds before exiting. finalize_cgi() calls a blocking
# waitpid(pid, NULL, 0), so a server that reaps synchronously stalls its whole
# single-threaded event loop here and cannot serve any other client until this
# child exits. Used by cgi_robustness_test's responsiveness probe.
import os
import sys
import time

body = "slow-exit-body\n"
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
# Close stdout so the server's read side sees EOF/HUP and moves to finalize.
os.close(sys.stdout.fileno())
# Stay alive: a synchronous waitpid in the server now blocks the event loop.
time.sleep(3)
