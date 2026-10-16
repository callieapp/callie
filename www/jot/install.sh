#!/bin/sh
# Jot installer - https://callieapp.org/jot
#
#   curl -fsSL https://callieapp.org/jot/install.sh | sh
#   curl -fsSL https://callieapp.org/jot/install.sh | sh -s -- --dry-run
#
# Runs the installer from Jot's latest release, which lives with Jot's source, so it
# always matches the packages it installs. Options are passed along to it.
set -eu

url="https://github.com/callieapp/jot/releases/latest/download/install.sh"
if ! script="$(curl --proto '=https' --tlsv1.2 -fsSL "$url")"; then
    printf 'jot: could not download the installer from %s\n' "$url" >&2
    exit 1
fi
printf '%s\n' "$script" | sh -s -- "$@"
