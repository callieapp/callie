# bash completion for callie. The candidates come from callie itself, which
# knows its commands, settings, calendars and events.
_callie() {
    local IFS=$'\n' line
    COMPREPLY=()
    for line in $(callie __complete "${COMP_WORDS[@]:1:COMP_CWORD}" 2>/dev/null); do
        COMPREPLY+=("$(printf '%q' "${line%%$'\t'*}")")
    done
}
complete -F _callie callie
