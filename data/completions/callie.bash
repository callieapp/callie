# bash completion for callie. The candidates come from callie itself, which
# knows its commands, settings, calendars and events.
_callie() {
    local IFS=$'\n' line typed=${COMP_LINE:0:COMP_POINT}
    # bash splits words at = and : too, and replaces only what follows the last
    # of them, so callie reads the line itself and that much of the word is cut
    # from the front of each candidate.
    local word=${typed##*[[:space:]]}
    local tail=${word##*[=:]}
    local ahead=${word%"$tail"}
    COMPREPLY=()
    for line in $(callie __complete --line "$typed" 2>/dev/null); do
        line=${line%%$'\t'*}
        [[ -n $ahead && $line == "$ahead"* ]] && line=${line#"$ahead"}
        COMPREPLY+=("$(printf '%q' "$line")")
    done
}
complete -F _callie callie
