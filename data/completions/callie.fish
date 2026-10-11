# fish completion for callie. The candidates come from callie itself, which
# knows its commands, settings, calendars and events.
complete -c callie -f -a '(callie __complete --line (commandline -cp) 2>/dev/null)'
