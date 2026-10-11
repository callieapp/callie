# bash completion for callie. The candidates come from callie itself, which
# knows its commands, settings, calendars and events.
_callie() {
    local IFS=$'\n' line typed=${COMP_LINE:0:COMP_POINT} cur=${COMP_WORDS[COMP_CWORD]}
    # bash splits words at = and : too, so callie reads the line itself, and what
    # bash already took as earlier words is cut from the front of each candidate.
    local word=${typed##*[[:space:]]}
    local ahead=${word%"$cur"}
    COMPREPLY=()
    for line in $(callie __complete --line "$typed" 2>/dev/null); do
        line=${line%%$'\t'*}
        [[ -n $ahead && $line == "$ahead"* ]] && line=${line#"$ahead"}
        COMPREPLY+=("$(printf '%q' "$line")")
    done
}
complete -F _callie callie
