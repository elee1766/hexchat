__module_name__ = "tlstest-py"
__module_version__ = "1"
__module_description__ = "Drives one /server connection for ci/tls-irc-test.sh"
import hexchat, os

OUT = os.path.join(hexchat.get_info("configdir"), "tlstest.out")
FLAG = os.environ["TLSTEST_FLAG"]      # -ssl or -ssl-noverify
HOST = os.environ["TLSTEST_HOST"]
PORT = os.environ["TLSTEST_PORT"]


def w(s):
    with open(OUT, "a") as f:
        f.write(s + "\n")


def done(*_):
    hexchat.command("quit")
    return 0


def on_event(name):
    def cb(word, word_eol, userdata):
        w("%s: %s" % (name, " | ".join(word)))
        if name in ("Connected", "Connection Failed"):
            hexchat.hook_timer(300, done)
        return hexchat.EAT_NONE
    return cb


for ev in ("Connected", "Connection Failed", "SSL Message", "Disconnected"):
    hexchat.hook_print(ev, on_event(ev))


def start(_):
    w("start")
    hexchat.command("server %s %s %s" % (FLAG, HOST, PORT))
    return 0


hexchat.hook_timer(500, start)
hexchat.hook_timer(20000, done)
