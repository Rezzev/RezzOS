export TERM="${TERM:-linux}"
export INPUTRC=/etc/inputrc
export HISTFILE="$HOME/.bash_history"
export HISTSIZE=10000
export HISTFILESIZE=10000
shopt -s histappend 2>/dev/null || true
PROMPT_COMMAND="history -a 2>/dev/null; $PROMPT_COMMAND"

alias ll='ls -la'
alias ..='cd ..'

# Launch tmux only if interactive, not already in tmux, and tmux works
if [ -z "$TMUX" ] && [ -t 0 ] && [ "$TERM" != "dumb" ] && command -v tmux >/dev/null 2>&1; then
    mkdir -p /dev/pts
    mount -t devpts devpts /dev/pts 2>/dev/null || true
    # Use && to only exec if tmux test passes; fallback to plain shell on failure
    tmux has-session -t rezzos 2>/dev/null && exec tmux attach-session -t rezzos
    tmux new-session -d -s rezzos 2>/dev/null && exec tmux attach-session -t rezzos
    # If tmux failed, continue with normal shell below
fi

clear
printf "Welcome to \033[0;37mRezz\033[0;34mOS\033[0m!\n"
