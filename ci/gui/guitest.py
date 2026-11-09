__module_name__ = "guitest-py"
__module_version__ = "1"
__module_description__ = "GUI test helper"
import hexchat, os

OUT = os.path.join(hexchat.get_info("configdir"), "guitest.out")


def w(s):
    with open(OUT, "a") as f:
        f.write(s + "\n")


def report(word, word_eol, ud):
    tabs = [c.channel for c in hexchat.get_list("channels")]
    w("tabs:" + ",".join(tabs))
    w("current:" + str(hexchat.get_info("channel")))
    w("report-done")
    return hexchat.EAT_ALL




def on_say(word, word_eol, ud):
    # record text sent with SAY/ECHO (the sysinfo plugin uses ECHO by default) without eating it
    w("cmd:" + word_eol[0])
    return hexchat.EAT_NONE


hexchat.hook_command("GUITEST", report)
hexchat.hook_command("SAY", on_say)
hexchat.hook_command("ECHO", on_say)
w("python-loaded")
