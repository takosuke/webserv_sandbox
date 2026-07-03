#!/usr/bin/env python3
# Exits immediately without writing anything. The server enters CGI_HEADERS,
# finds no "\r\n" and returns BEFORE the EPOLLHUP check, so once this child exits
# the level-triggered EPOLLHUP re-fires every loop iteration and the connection
# is never finalized or closed -> 100% CPU spin, no response. Used by
# cgi_robustness_test's no-output timeout probe.
import sys
sys.exit(0)
