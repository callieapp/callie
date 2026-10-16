#!/bin/sh
# Prints a Flatpak manifest for Callie Devel, made from the main one: its own
# app id, folders and keyring entries, so it runs beside an installed Callie.
#
#   scripts/flatpak-devel.sh > packaging/flatpak/org.callieapp.Callie.Devel.yml
set -eu

manifest="$(dirname "$0")/../packaging/flatpak/org.callieapp.Callie.yml"
sed -e 's/^app-id: org\.callieapp\.Callie$/app-id: org.callieapp.Callie.Devel/' \
    -e '/^command: /a\
rename-desktop-file: org.callieapp.Callie.desktop\
rename-icon: org.callieapp.Callie\
rename-appdata-file: org.callieapp.Callie.metainfo.xml' \
    -e '/^      - -DCALLIE_TUI=ON$/a\
      - -DCALLIE_DEVEL=ON' \
    -e '/^    run-tests: true$/i\
    post-install:\
      - sed -i "s/^Name=Callie$/Name=Callie Devel/" /app/share/applications/*.desktop' \
    "$manifest"
