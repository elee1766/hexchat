#!/bin/sh
# End-to-end TLS test of the real server.c connect path (loopback only).
# usage: ci/tls-irc-test.sh BUILD_DIR   (needs openssl CLI, python3, the built
#        hexchat-text and python plugin)
#
# hexchat-text, driven by a python plugin, runs `/server -ssl 127.0.0.1 PORT`
# against local TLS IRC servers on 127.0.0.1 (ci/tls/tls-irc-server.py).  The
# test CA is supplied through SSL_CERT_FILE (read by OpenSSL's default verify
# paths, which HexChat uses).  Scenarios:
#   trusted        CA is trusted, cert has IP SAN 127.0.0.1  -> connects, registers
#   hostname       trusted CA, cert only for wrong.example   -> refused
#   unknown-ca     cert for 127.0.0.1 from an untrusted CA   -> refused
# Controls: each refused server is also connected with -ssl-noverify, which
# must succeed, so a refusal is due to verification and not to a broken setup.
set -u
BUILD=$(cd "${1:?build dir}" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
FAILS=0
SRV_PID=
export PYTHONPATH="$HERE/../plugins/python" LC_ALL=C.UTF-8 TLSTEST_HOST=127.0.0.1
# no external network is needed or used; make sure no proxy variable interferes
unset http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY

pass() { echo "ok - $1"; }
fail() { echo "not ok - $1"; FAILS=$((FAILS+1)); }
cleanup() {
	[ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null
	[ "$FAILS" -ne 0 ] && echo "# artifacts kept in $T" || rm -rf "$T"
}
trap cleanup EXIT

command -v openssl >/dev/null || { echo "openssl CLI required"; exit 2; }
[ -x "$BUILD/src/fe-text/hexchat-text" ] || { echo "hexchat-text not built"; exit 2; }

# --- certificates: trusted CA, untrusted CA, three server certs ---
mkca() { # name
	openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=$1" \
		-keyout "$T/$1.key" -out "$T/$1.pem" \
		-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign" 2>/dev/null
}
mkleaf() { # name ca san
	openssl req -newkey rsa:2048 -nodes -subj "/CN=$1" -keyout "$T/$1.key" -out "$T/$1.csr" 2>/dev/null
	printf 'subjectAltName=%s\nbasicConstraints=CA:FALSE\n' "$3" >"$T/$1.ext"
	openssl x509 -req -in "$T/$1.csr" -CA "$T/$2.pem" -CAkey "$T/$2.key" -CAcreateserial \
		-days 2 -extfile "$T/$1.ext" -out "$T/$1.pem" 2>/dev/null
}
mkca testca; mkca rogueca
mkleaf good testca "IP:127.0.0.1"
mkleaf wronghost testca "DNS:wrong.example"
mkleaf rogue rogueca "IP:127.0.0.1"
export SSL_CERT_FILE="$T/testca.pem" SSL_CERT_DIR="$T/none"

# run NAME CERT FLAG -> sets $CLIENT_OUT and $SERVER_LOG
run() {
	name=$1; cert=$2; flag=$3
	SERVER_LOG=$T/$name.srv; CLIENT_OUT=$T/$name.cfg/tlstest.out
	rm -f "$T/$name.port"; : >"$SERVER_LOG"
	python3 -I "$HERE/tls/tls-irc-server.py" "$T/$cert.pem" "$T/$cert.key" "$T/$name.port" "$SERVER_LOG" &
	SRV_PID=$!
	i=0; while [ ! -s "$T/$name.port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	[ -s "$T/$name.port" ] || { fail "$name: server started"; return 1; }
	cfg=$T/$name.cfg; mkdir -p "$cfg/addons"
	ln -s "$(ls "$BUILD"/plugins/python/python.so)" "$cfg/addons/python.so"
	cp "$HERE/tls/tlstest.py" "$cfg/addons/"
	printf 'irc_nick1 = tlstest\nirc_user_name = tlstest\nirc_real_name = tlstest\n' >"$cfg/hexchat.conf"
	# stdin kept open and silent: hexchat-text stops when it reads EOF
	TLSTEST_PORT=$(cat "$T/$name.port") TLSTEST_FLAG=$flag \
		timeout 40 sh -c 'sleep 30 | exec "$0" -d "$1" --no-auto' \
		"$BUILD/src/fe-text/hexchat-text" "$cfg" >"$T/$name.log" 2>&1
	kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; SRV_PID=
	return 0
}
saw() { grep -q -- "$2" "$1"; }

expect_connect() { # name cert flag
	run "$1" "$2" "$3" || return
	if saw "$CLIENT_OUT" '^Connected:' && saw "$SERVER_LOG" '^registered tlstest' \
	   && saw "$SERVER_LOG" '^tls-ok TLSv1\.[23]'; then
		pass "$1: connected, TLS handshake done, IRC registration reached the server"
	else fail "$1: expected a connection"; cat "$CLIENT_OUT" "$SERVER_LOG" 2>/dev/null; fi
}
expect_refuse() { # name cert flag pattern
	run "$1" "$2" "$3" || return
	if saw "$CLIENT_OUT" "^Connection Failed:.*$4" && ! saw "$CLIENT_OUT" '^Connected:' \
	   && ! saw "$SERVER_LOG" '^registered'; then
		pass "$1: refused ($4) and no IRC registration was sent"
	else fail "$1: expected refusal with '$4'"; cat "$CLIENT_OUT" "$SERVER_LOG" 2>/dev/null; fi
}

expect_connect trusted good -ssl
expect_refuse hostname wronghost -ssl 'hostname'
expect_refuse unknown-ca rogue -ssl 'unable to verify the first certificate'
expect_connect hostname-noverify wronghost -ssl-noverify
expect_connect unknown-ca-noverify rogue -ssl-noverify

[ "$FAILS" -eq 0 ] && echo "all TLS IRC checks passed" || echo "$FAILS check(s) failed"
exit "$FAILS"
