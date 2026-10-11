#!/bin/sh
# MHFZ Plus setup for Linux: installs MHFZ Plus into the Wine prefix the game runs in (Steam/Proton, Lutris, Bottles,
# Heroic or plain Wine). Run it from the extracted package:  sh install.sh   (--terminal: questions here, no window)
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
    msg="MHFZ Plus needs Python 3 (python3). Install it with your package manager, then run install.sh again."
    echo "$msg" >&2
    command -v zenity >/dev/null 2>&1 && zenity --error --no-markup --text "$msg" 2>/dev/null
    exit 1
fi
exec python3 setup/linux/mhfzplus.py install "$@"
