#!/bin/bash
# Checks the bash completion script against the built callie on sample data:
# what it offers to put in place of the word being typed.
#
#   tests/completion_test.sh data/completions/callie.bash build/src/cli/callie
set -u

script="$1"
bin="$(mktemp -d)"
trap 'rm -rf "$bin"' EXIT
ln -s "$(realpath "$2")" "$bin/callie"
export PATH="$bin:$PATH"
# Keeps callie's settings, data and log away from the user's own.
export XDG_CONFIG_HOME="$bin/config" XDG_DATA_HOME="$bin/data" XDG_STATE_HOME="$bin/state" \
    XDG_CACHE_HOME="$bin/cache"
# shellcheck disable=SC1090
source "$script"
failures=0

# Offers for `line`, with the cursor at its end, as bash would ask.
offers() {
    COMP_LINE="$1"
    COMP_POINT=${#COMP_LINE}
    COMP_WORDS=()
    COMP_CWORD=0
    _callie
    printf '%s\n' "${COMPREPLY[@]}"
}

expect() {
    local got
    got="$(offers "$1")"
    if ! printf '%s\n' "$got" | grep -qxF -- "$2"; then
        printf 'FAIL: for "%s" expected "%s" among:\n%s\n\n' "$1" "$2" "$got"
        failures=$((failures + 1))
    fi
}

expect "callie --sample ed" "edit"
expect "callie --sample edit x --scope " "following"
# After = or :, bash puts only what follows it in place.
expect "callie --sample edit x --scope=" "this"
expect "callie --sample edit x --scope=f" "following"
# Names with spaces come quoted for the shell.
expect "callie --sample calendars hide Fo" "Focus"
expect "callie -d 3 ag" "agenda"
expect "callie --he" "--help"
expect "callie --ver" "--version"

# Escaped and quoted spaces, against a stand-in callie offering one name.
callie() { printf '%s\n' "$candidate"; }
candidate="Work: team"
expect 'callie add x --calendar Work:\ t' '\ team'
expect 'callie add x --calendar "Work: t' 'Work: team'
candidate="--calendar=Deep work"
expect 'callie add x --calendar=Deep\ w' 'Deep\ work'
expect 'callie add x --calendar="Deep w' 'Deep work'
# Without : as a word break, bash replaces the whole word.
candidate="Work: team"
COMP_WORDBREAKS=${COMP_WORDBREAKS//:/} expect 'callie add x --calendar Work:\ t' 'Work:\ team'

# Every command in the help keeps a space before its description.
if command callie --help | grep -E '^  [a-z-]+[A-Z]'; then
    printf 'FAIL: a command runs into its description in the help\n\n'
    failures=$((failures + 1))
fi
usage="$(command callie completions 2>&1)"
if [ $? != 2 ] || ! printf '%s\n' "$usage" | grep -qxF "usage: callie completions bash|zsh|fish"; then
    printf 'FAIL: "callie completions" without a shell does not show its usage:\n%s\n\n' "$usage"
    failures=$((failures + 1))
fi

if [ "$failures" -gt 0 ]; then
    printf '%s check(s) failed\n' "$failures"
    exit 1
fi
printf 'all completion checks passed\n'
