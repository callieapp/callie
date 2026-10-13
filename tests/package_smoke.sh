#!/bin/sh
# Runs an installed Callie as a user would, to catch files and dependencies a
# package leaves out. Writes the app's window to the folder given.
set -eu

shots="${1:?usage: package_smoke.sh <folder for the screenshot>}"
home="$(mktemp -d)"
export HOME="$home" XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" \
    XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state"
now=2026-03-18T10:40

callie --version
callie agenda --sample --now "$now" | grep -q Standup
for file in /usr/share/bash-completion/completions/callie \
    /usr/share/zsh/site-functions/_callie \
    /usr/share/fish/vendor_completions.d/callie.fish \
    /usr/share/applications/org.callieapp.Callie.desktop; do
    test -s "$file" || { echo "missing $file" >&2; exit 1; }
done

mkdir -p "$shots"
QT_QPA_PLATFORM=offscreen callie-gui --sample --now "$now" --size 1280x840 \
    --screenshot "$shots/installed.png"
test -s "$shots/installed.png"
echo "the installed app runs"
