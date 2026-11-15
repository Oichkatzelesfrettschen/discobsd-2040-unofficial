#!/bin/sh
# Start the MATEzinho desktop on the PicoCalc panel with the icon pack the
# SD root carries; notes save to ~/notes.txt. Ctrl-W closes a view and Esc
# returns to the launcher (Help lists every key).
exec /usr/bin/matezinho /usr/share/matezinho/icons.mzar "${HOME:-/tmp}/notes.txt"
