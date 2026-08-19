#!/usr/bin/env python3
# Like hang_forever.py, but it also ignores SIGTERM — so the polite half of the
# server's escalation has no effect and only SIGKILL can stop it.
#
# This is the script that catches a reaper which *logs* an escalation without
# performing it: with SIGTERM ignored and SIGKILL never actually sent, the
# process runs until the machine is rebooted. Used by cgi_lifecycle_test.
import signal
import time

signal.signal(signal.SIGTERM, signal.SIG_IGN)

while True:
    time.sleep(0.5)
