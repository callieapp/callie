#!/bin/sh
# Jot's installer moved to https://jotwriter.org/install.sh. This address keeps
# working for copies of Jot and instructions that still name it, and runs the
# installer from there. Options are passed along to it.
set -eu

url="https://jotwriter.org/install.sh"
if ! script="$(curl --proto '=https' --tlsv1.2 -fsSL "$url")"; then
    printf 'jot: could not download the installer from %s\n' "$url" >&2
    exit 1
fi
printf '%s\n' "$script" | sh -s -- "$@"
