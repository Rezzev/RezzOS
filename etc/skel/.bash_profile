# Bash login shell profile: source the interactive rc so aliases, prompt
# and the tmux session apply on getty/ssh/su logins, not just subshells.
[ -f "$HOME/.bashrc" ] && . "$HOME/.bashrc"
