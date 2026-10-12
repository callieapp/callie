# bash completion for callie. The candidates come from callie itself, which
# knows its commands, settings, calendars and events.
_callie() {
    local IFS=$'\n' line typed=${COMP_LINE:0:COMP_POINT}
    # The word being typed, as typed: from the last space that is not escaped
    # or quoted. bash also splits it at an = or : outside quotes, while they are
    # in COMP_WORDBREAKS as by default, and replaces only what follows, so
    # callie reads the line itself and that much of the word is cut from the
    # front of each candidate.
    local word='' quote='' ahead='' c i
    for ((i = 0; i < ${#typed}; i++)); do
        c=${typed:i:1}
        if [[ $c == "\\" && $quote != "'" ]]; then
            word+=${typed:i:2}
            ((i++))
        elif [[ -n $quote ]]; then
            [[ $c == "$quote" ]] && quote=''
            word+=$c
        elif [[ $c == [[:space:]] ]]; then
            word=''
            ahead=''
        else
            [[ $c == [\"\'] ]] && quote=$c
            word+=$c
            [[ $c == [=:] && $COMP_WORDBREAKS == *"$c"* ]] && ahead=$word
        fi
    done
    ahead=${ahead//[\\\"\']/}
    COMPREPLY=()
    for line in $(callie __complete --line "$typed" 2>/dev/null); do
        line=${line%%$'\t'*}
        [[ -n $ahead && $line == "$ahead"* ]] && line=${line#"$ahead"}
        # Inside an open quote, bash quotes what it puts in place itself.
        if [[ -n $quote ]]; then
            COMPREPLY+=("$line")
        else
            COMPREPLY+=("$(printf '%q' "$line")")
        fi
    done
    # A folder is a step on the way to a file, not the end of the word.
    [[ ${#COMPREPLY[@]} -eq 1 && ${COMPREPLY[0]} == */ ]] && compopt -o nospace
}
complete -F _callie callie
