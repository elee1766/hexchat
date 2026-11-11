#!/usr/bin/env python3
"""Minimal loopback-only TLS IRC server for ci/tls-irc-test.sh.

usage: tls-irc-server.py CERT KEY PORT_FILE EVENT_LOG

Binds 127.0.0.1 on an ephemeral port (written to PORT_FILE), accepts TLS
clients, answers NICK/USER with a 001 welcome, PING with PONG, and exits on
QUIT.  Every step is appended to EVENT_LOG as one line:
  tls-failed <reason>   handshake rejected (client aborted or protocol error)
  tls-ok <version>      handshake completed
  registered <nick>     client sent NICK and USER
It never connects anywhere and never binds a non-loopback address.
"""
import os
import socket
import ssl
import sys
import threading

cert, key, port_file, log_file = sys.argv[1:5]
log_lock = threading.Lock()


def log(msg):
    with log_lock, open(log_file, "a") as f:
        f.write(msg + "\n")


ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(cert, key)

lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
lsock.bind(("127.0.0.1", 0))
lsock.listen(4)
with open(port_file + ".tmp", "w") as f:
    f.write(str(lsock.getsockname()[1]))
os.rename(port_file + ".tmp", port_file)


def serve(raw):
    raw.settimeout(15)
    try:
        conn = ctx.wrap_socket(raw, server_side=True)
    except (ssl.SSLError, OSError) as e:
        log("tls-failed %s" % (e.__class__.__name__,))
        raw.close()
        return
    log("tls-ok %s" % conn.version())
    nick = user = None
    buf = b""
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                parts = line.decode("utf-8", "replace").strip().split(" ")
                cmd = parts[0].upper()
                if cmd == "NICK" and len(parts) > 1:
                    nick = parts[1]
                elif cmd == "USER":
                    user = True
                elif cmd == "PING":
                    conn.sendall(("PONG %s\r\n" % " ".join(parts[1:])).encode())
                elif cmd == "QUIT":
                    return
                if nick and user and cmd in ("NICK", "USER"):
                    log("registered %s" % nick)
                    conn.sendall((":tls.test 001 %s :Welcome\r\n" % nick).encode())
                    nick = user = None
    except (ssl.SSLError, OSError):
        pass
    finally:
        conn.close()


while True:
    s, _ = lsock.accept()
    threading.Thread(target=serve, args=(s,), daemon=True).start()
