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

if [ "$failures" -gt 0 ]; then
    printf '%s check(s) failed\n' "$failures"
    exit 1
fi
printf 'all completion checks passed\n'
