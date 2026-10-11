# fish completion for callie. The candidates come from callie itself, which
# knows its commands, settings, calendars and events.
function __callie_complete
    set -l words (commandline -opc)
    callie __complete $words[2..-1] (commandline -ct) 2>/dev/null
end
complete -c callie -f -a '(__callie_complete)'
