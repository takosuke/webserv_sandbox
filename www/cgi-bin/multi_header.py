#!/usr/bin/env python3
import sys

body = "multi-header-marker\n"

sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("X-First: alpha-value-4242\r\n")
sys.stdout.write("X-Second: beta\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
