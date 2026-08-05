#!/usr/bin/env python3
# Writes nothing and never exits: the classic runaway CGI an evaluator reaches
# for. The server holds it in CGI_HEADERS with a pipe that never delivers a
# byte, so no per-connection timeout can bound it either — update_timestamp()
# only fires on data. Only a deadline on the child process itself ends this.
#
# Used by cgi_lifecycle_test: the server must SIGTERM it at CGI_TIMEOUT, and
# the resulting pipe EOF must drive the connection to an answer.
import time

while True:
    time.sleep(0.5)
