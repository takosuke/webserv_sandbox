#!/usr/bin/env python3
import os
import sys

body = "Hello, World!" + sys.stdin.read() + "!!!!\n"

sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 400 Bad Request\r\n")
sys.stdout.write("Content-Length: " + str(len(body)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
