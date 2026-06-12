#!/usr/bin/env python3
import os, sys

method = os.environ.get('REQUEST_METHOD', '')
query  = os.environ.get('QUERY_STRING', '')
body   = sys.stdin.read()

output = "METHOD=" + method + "\nQUERY=" + query + "\nBODY=" + body + "\n"

sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Length: " + str(len(output)) + "\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(output)
sys.stdout.flush()
