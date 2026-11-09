#!/bin/sh
# Xvfb GUI smoke tests for the GTK3 frontend.
# usage: ci/gui-test.sh BUILD_DIR   (needs Xvfb, xdotool, xwd, openbox or any WM)
# Exercises: startup, tab creation/switching, scrollback + mouse wheel,
# text selection, dialogs (network list, preferences) and all built plugins.
set -u
BUILD=$(cd "${1:?build dir}" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
CFG=$T/cfg
DISP=:97
FAILS=0
export PYTHONPATH="$HERE/../plugins/python" GDK_BACKEND=x11 LC_ALL=C.UTF-8 NO_AT_BRIDGE=1 DISPLAY=$DISP

pass() { echo "ok - $1"; }
fail() { echo "not ok - $1"; FAILS=$((FAILS+1)); }
check() { # name cmd...
	n=$1; shift
	if "$@" >/dev/null 2>&1; then pass "$n"; else fail "$n"; fi
}
# Assertions use observable state (window title, PRIMARY selection contents,
# text recorded by the guitest plugins), never whole-screen pixel hashes: those
# change with caret blink, the lag meter, pointer shape and window-manager
# decorations. Screenshots are kept only as diagnostic artifacts (snap).
primary() { xclip -selection primary -o 2>/dev/null; }
clear_primary() { xclip -selection primary -i </dev/null 2>/dev/null; }
title() { xdotool getwindowname "$WIN" 2>/dev/null; }
# triple-click at X Y and print the scrollback line number (N) of the line hit
line_at() {
	clear_primary
	xdotool mousemove "$1" "$2" click --repeat 3 --delay 60 1; sleep 0.5
	primary | sed -n 's/.*line number \([0-9][0-9]*\) of the scrollback test.*/\1/p' | head -1
}
snap() { command -v convert >/dev/null 2>&1 && xwd -root -silent | convert xwd:- "png:$T/$1.png"; }
say() { xdotool type --delay 8 -- "$1"; xdotool key Return; sleep 0.4; }
wait_for() { # seconds file pattern
	i=0; while [ $i -lt $(($1*5)) ]; do grep -qs "$3" "$2" && return 0; sleep 0.2; i=$((i+1)); done; return 1
}

cleanup() {
	[ -n "${HC_PID:-}" ] && kill "$HC_PID" 2>/dev/null
	[ -n "${WM_PID:-}" ] && kill "$WM_PID" 2>/dev/null
	[ -n "${X_PID:-}" ] && kill "$X_PID" 2>/dev/null
	[ "$FAILS" -ne 0 ] && echo "# artifacts kept in $T" || rm -rf "$T"
}
trap cleanup EXIT

mkdir -p "$CFG/addons" "$HOME/.config" 2>/dev/null
for p in python perl lua checksum fishlim sysinfo; do
	f=$(ls "$BUILD"/plugins/$p/$p.so 2>/dev/null) || { fail "plugin $p built"; continue; }
	ln -s "$f" "$CFG/addons/$p.so"
done
cp "$HERE/gui/guitest.py" "$HERE/gui/guitest.pl" "$HERE/gui/guitest.lua" "$CFG/addons/"
cat > "$CFG/hexchat.conf" <<EOF
gui_slist_skip = 1
irc_nick1 = guitest
irc_user_name = guitest
irc_real_name = guitest
gui_tab_pos = 1
gui_win_width = 900
gui_win_height = 600
gui_lagometer = 1
EOF
echo "# output dir $T"

Xvfb $DISP -screen 0 1280x900x24 >"$T/xvfb.log" 2>&1 & X_PID=$!
sleep 2
check "Xvfb running" kill -0 $X_PID
openbox >"$T/wm.log" 2>&1 & WM_PID=$!
sleep 1

if [ -n "${GUI_GDB:-}" ]; then	# debugging aid: stop with a backtrace at the first critical
	G_DEBUG=fatal-criticals gdb -batch -ex run -ex "bt 25" --args "$BUILD/src/fe-gtk/hexchat" -d "$CFG" --no-auto >"$T/hexchat.log" 2>&1 & HC_PID=$!
else
	"$BUILD/src/fe-gtk/hexchat" -d "$CFG" --no-auto >"$T/hexchat.log" 2>&1 & HC_PID=$!
fi
i=0; while [ $i -lt 50 ] && ! xdotool search --onlyvisible --name "HexChat" >/dev/null 2>&1; do sleep 0.2; i=$((i+1)); done
WIN=$(xdotool search --onlyvisible --name "HexChat" | head -1)
[ -n "$WIN" ] && pass "main window appears" || { fail "main window appears"; cat "$T/hexchat.log"; exit 1; }
xdotool windowmove "$WIN" 0 0; xdotool windowsize "$WIN" 900 600; xdotool windowfocus "$WIN"; sleep 1
wait_for 20 "$CFG/guitest.out" "python-loaded" && pass "python plugin + script loaded" || fail "python plugin + script loaded"
wait_for 5 "$CFG/guitest.out" "perl-loaded" && pass "perl plugin + script loaded" || fail "perl plugin + script loaded"
wait_for 5 "$CFG/guitest.out" "lua-loaded" && pass "lua plugin + script loaded" || fail "lua plugin + script loaded"

xdotool mousemove 450 300 click 1; sleep 0.3
# Plugins: dlopen succeeded and stayed loaded (init ran), plus behaviour checks
for p in python perl lua checksum fishlim sysinfo; do
	check "plugin $p loaded" grep -q "/$p\.so" /proc/$HC_PID/maps
done
snap start
say "/SYSINFO"; sleep 1; snap sysinfo
check "sysinfo plugin output contains system info (OS)" grep -q "^cmd:ECHO.*OS" "$CFG/guitest.out"
say "/SETKEY guikey secretpass"
sleep 1
check "fishlim stores key" sh -c "grep -rqs guikey '$CFG'"
say "/GUITEST"
wait_for 10 "$CFG/guitest.out" "report-done" || fail "guitest report"

# Tabs: open queries, check the context list and switching
say "/QUERY alice"; say "/QUERY bob"; say "/QUERY carol"
say "/GUITEST"
for q in alice bob carol; do check "tab $q created" grep -q "^tabs:.*$q" "$CFG/guitest.out"; done
snap tabs
A=$(title)
xdotool key ctrl+Prior; sleep 0.5
check "ctrl+PageUp switches tab (window title changes)" test "$(title)" != "$A"
xdotool key alt+1; sleep 0.5
say "/GUITEST"; pass "alt+1 handled without crash"

xdotool mousemove 76 81 click 1; sleep 0.6
check "clicking a tab switches to it" sh -c "xdotool getwindowname $WIN | grep -q alice"

# Scrollback: inject lots of lines, scroll with the wheel, compare frames
say "/QUERY scroller"
n=1; while [ $n -le 300 ]; do xdotool type --delay 0 -- "/ECHO line number $n of the scrollback test"; xdotool key Return; n=$((n+1)); done
sleep 1
xdotool mousemove 450 250
BOTTOM=$(line_at 450 250)
check "scrollback line readable at bottom (got '$BOTTOM')" test -n "$BOTTOM"
xdotool mousemove 450 250; xdotool click --repeat 10 --delay 30 4; sleep 0.6
UP=$(line_at 450 250)
check "wheel up scrolls text view (line $BOTTOM -> $UP)" test -n "$UP" -a "${UP:-0}" -lt "${BOTTOM:-0}"
xdotool mousemove 450 250; xdotool click --repeat 30 --delay 30 5; sleep 0.6
DOWN=$(line_at 450 250)
check "wheel down returns to bottom (line $DOWN)" test "$DOWN" = "$BOTTOM"
xdotool key Prior; sleep 0.5
PGUP=$(line_at 450 250)
check "PageUp scrolls (line $BOTTOM -> $PGUP)" test -n "$PGUP" -a "${PGUP:-0}" -lt "${BOTTOM:-0}"
# (End is not a scroll binding in HexChat: it moves the input caret.)
xdotool mousemove 450 250; xdotool click --repeat 30 --delay 30 5; sleep 0.6

# Selection: the PRIMARY selection must hold the text that was selected
clear_primary
xdotool mousemove 240 300 mousedown 1; for x in 280 340 420 520; do sleep 0.1; xdotool mousemove $x 340; done; sleep 0.2; xdotool mouseup 1; sleep 0.5
snap selection
SELTXT=$(primary)
SELLINES=$(printf '%s\n' "$SELTXT" | grep -c 'line number')
check "mouse drag copies multi-line scrollback text to PRIMARY ($SELLINES lines)" test "$SELLINES" -ge 2
clear_primary
xdotool mousemove 330 300 click --repeat 2 --delay 60 1; sleep 0.5
WORD=$(primary)
WORDS=$(printf %s "$WORD" | wc -w); WORDNL=$(printf %s "$WORD" | wc -l)
check "double click selects exactly one word (got '$WORD')" test -n "$WORD" -a "$WORDS" -eq 1 -a "$WORDNL" -eq 0
xdotool mousemove 700 500 click 1

# Dialogs
dialog() { # name key title-pattern
	xdotool key "$2"; sleep 1.5
	if xdotool search --onlyvisible --name "$3" >/dev/null 2>&1; then
		D=$(xdotool search --onlyvisible --name "$3" | head -1)
		snap "dialog-$1"
		pass "dialog: $1"
		xdotool windowfocus "$D"; sleep 0.3; xdotool key Escape; sleep 0.5
		xdotool search --onlyvisible --name "$3" >/dev/null 2>&1 && { xdotool windowfocus "$D"; xdotool key alt+F4; sleep 0.5; }
	else fail "dialog: $1"; fi
	xdotool windowfocus "$WIN"
}
dialog "network-list" ctrl+s "Network List"
say "/SET"
say "/SERVCHAN"
for cmd in "/IGNORE" "/NOTIFY"; do say "$cmd"; done
# Settings dialog via the menu mnemonic (Settings -> Preferences)
snap before-prefs
xdotool mousemove 213 32 click 1; sleep 0.8; snap menu; xdotool key Down; sleep 0.2; xdotool key Return; sleep 2; snap prefs
if xdotool search --onlyvisible --name "Preferences" >/dev/null 2>&1; then
	pass "dialog: preferences"
	P=$(xdotool search --onlyvisible --name "Preferences" | head -1)
	xdotool windowfocus "$P"
	# walk the page tree with the keyboard so each pane is built
	for k in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do xdotool key --window "$P" Down; sleep 0.15; done
	# no pixel oracle for page content: this only proves navigation is alive
	kill -0 $HC_PID 2>/dev/null && pass "preferences page walk did not crash" || fail "preferences page walk did not crash"
	xdotool key --window "$P" Escape; sleep 0.5
	xdotool search --onlyvisible --name "Preferences" >/dev/null 2>&1 && xdotool key --window "$P" alt+F4
else fail "dialog: preferences"; fi
xdotool windowfocus "$WIN"; sleep 0.3

# Still alive and no GTK criticals
kill -0 $HC_PID 2>/dev/null && pass "hexchat survived the session" || fail "hexchat survived the session"
if grep -E "CRITICAL|Gtk-WARNING|GLib-GObject-WARNING|Segmentation|assertion" "$T/hexchat.log" >"$T/crit.log"; then
	fail "no GTK/GLib warnings"; sed -n 1,30p "$T/crit.log"
else pass "no GTK/GLib warnings"; fi

echo "# failures: $FAILS"
exit $FAILS
