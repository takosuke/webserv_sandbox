#!/bin/bash
# Verifies Gaps_and_Issues.md issue 10: accept() failing with EMFILE must not
# busy-spin the event loop.
#
# Under level-triggered epoll a failed accept() leaves the listen fd readable,
# so epoll_wait returns immediately forever. The fix surrenders EpollLoop's
# reserve fd, accepts-and-closes the pending connection to drain the backlog
# entry, then reclaims the reserve.
#
# Not a kaocha test: make-fixture launches the server via ProcessBuilder with no
# way to set an rlimit, and fd-starvation timing is inherently flaky.
#
#   PASS -> STARVED reads ~0% CPU
#   FAIL -> STARVED reads ~99% CPU
#
# Usage: webserv-tests/manual/emfile_spin.sh      (from the repo root)

set -u
cd "$(dirname "$0")/../.." || exit 1

CONF=conf/generated/base.conf
[ -f "$CONF" ] || { echo "missing $CONF — run 'make prepare-confs' first"; exit 1; }
[ -x ./webserv ] || { echo "missing ./webserv — run 'make' first"; exit 1; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"; pkill -9 -x webserv 2>/dev/null' EXIT
pkill -9 -x webserv 2>/dev/null; sleep 0.5

# The ulimit must apply to the server subshell ONLY. Leaking it into the python
# client starves the client instead, which looks identical but proves nothing.
# disown so bash does not report "Killed" when the job is pkill'd below.
( ulimit -n 64; exec ./webserv "$CONF" > "$TMP/srv.log" 2>&1 ) &
disown
sleep 1

SRV=$(pgrep -x webserv | head -1)
[ -n "$SRV" ] || { echo "server failed to start"; cat "$TMP/srv.log"; exit 1; }

cpu() { awk '{print $14+$15}' "/proc/$SRV/stat"; }   # utime+stime, in ticks

A=$(cpu); sleep 3; B=$(cpu)
echo "IDLE    : $((B-A)) ticks over 3s  (~$(((B-A)*100/300))% CPU)"

python3 - <<'PY' &
import socket, time
hold = []
for _ in range(200):
    try:
        hold.append(socket.create_connection(('127.0.0.1', 8080), 1))
    except Exception:
        pass
time.sleep(8)
PY
CLIENT=$!
sleep 2

C=$(cpu); sleep 3; D=$(cpu)
echo "STARVED : $((D-C)) ticks over 3s  (~$(((D-C)*100/300))% CPU)"

kill "$CLIENT" 2>/dev/null; wait "$CLIENT" 2>/dev/null

if [ $(((D-C)*100/300)) -lt 25 ]; then
	echo "PASS — no busy-spin under fd exhaustion"
else
	echo "FAIL — busy-spin (issue 10 regressed)"
	exit 1
fi
