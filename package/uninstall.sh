#!/bin/sh
# Removes MHFZ Plus on Linux, original game files back first (the same as "Uninstall MHFZ Plus" in the menu).
cd "$(dirname "$0")" || exit 1
exec python3 setup/linux/mhfzplus.py uninstall "$@"
