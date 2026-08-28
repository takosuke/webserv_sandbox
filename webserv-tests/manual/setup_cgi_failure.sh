#!/bin/bash
# Verifies Gaps_and_Issues.md issue 7: a setup_cgi() failure must answer, not
# hang the connection.
#
# setup_cgi() returns false only when pipe() or fork() fails, so the only way to
# reach it is to starve the server of descriptors. It used to set status 500 and
# return without calling setup_res() or rearming for EPOLLOUT; since _state is
# already RESPONSE (8) and handle_timeout()'s guard is _state < DISCARD_BODY (4),
# the connection was dropped 60s later having sent zero bytes.
#
# The fd budget matters. Too tight and accept() itself fails (that is issue 10,
# a different bug with the same symptom), so the script sweeps several values and
# prints whether the request reached the parser. Only rows with "parsed=yes"
# exercise issue 7 — rows with "parsed=no" are the accept path.
#
#   PASS -> every parsed=yes row answers 500
#   FAIL -> a parsed=yes row reports "NO RESPONSE"
#
# Usage: webserv-tests/manual/setup_cgi_failure.sh   (from the repo root)

set -u
cd "$(dirname "$0")/../.." || exit 1

CONF=conf/generated/base.conf
[ -f "$CONF" ] || { echo "missing $CONF — run 'make prepare-confs' first"; exit 1; }
[ -x ./webserv ] || { echo "missing ./webserv — run 'make' first"; exit 1; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"; pkill -9 -x webserv 2>/dev/null' EXIT
RC=0
SAW_PARSED=0

for HOLD in 54 56 57 58; do
	pkill -9 -x webserv 2>/dev/null; sleep 0.5
	# disown so bash does not report "Killed" when the job is pkill'd below.
	( ulimit -n 64; exec ./webserv "$CONF" > "$TMP/srv.log" 2>&1 ) &
	disown
	sleep 1

	RES=$(python3 - "$HOLD" <<'PY'
import socket, sys
n = int(sys.argv[1]); hold = []
for _ in range(n):
    try:
        hold.append(socket.create_connection(('127.0.0.1', 8080), 1))
    except Exception:
        break
try:
    c = socket.create_connection(('127.0.0.1', 8080), 2)
    c.sendall(b'GET /cgi-bin/hello.py HTTP/1.0\r\nHost: x\r\n\r\n')
    c.settimeout(5)
    d = c.recv(4096)
    print(d.split(b'\r\n')[0].decode() if d else 'EMPTY')
except socket.timeout:
    print('NO RESPONSE')
except Exception as e:
    print('conn error: %s' % e)
PY
)
	if grep -q "hello.py" "$TMP/srv.log" 2>/dev/null; then PARSED=yes; else PARSED=no; fi
	echo "held=$HOLD parsed=$PARSED -> $RES"

	if [ "$PARSED" = yes ]; then
		SAW_PARSED=1
		case "$RES" in
			*"500"*) ;;
			*) echo "  ^ FAIL: reached setup_cgi but did not answer 500"; RC=1 ;;
		esac
	fi
	pkill -9 -x webserv 2>/dev/null
done

if [ "$SAW_PARSED" = 0 ]; then
	echo "INCONCLUSIVE — no request reached the parser; widen the HOLD sweep"
	exit 2
fi
[ "$RC" = 0 ] && echo "PASS — setup_cgi() failure is answered, not hung"
exit "$RC"
