export TERM="${TERM:-linux}"
export INPUTRC=/etc/inputrc
export HISTFILE=/root/.bash_history
export HISTSIZE=10000
export HISTFILESIZE=10000
shopt -s histappend 2>/dev/null || true
PROMPT_COMMAND="history -a 2>/dev/null; $PROMPT_COMMAND"

PS1='\[\e[1;34m\]\u\[\e[0m\]@\h:\w\$> '
alias ll='ls -la'
alias ..='cd ..'

if [ -z "$TMUX" ] && [ -t 0 ] && [ "$TERM" != "dumb" ] && command -v tmux >/dev/null 2>&1; then
    mkdir -p /dev/pts
    mount -t devpts devpts /dev/pts 2>/dev/null || true
    exec tmux new-session -A -s rezzos /bin/bash --rcfile /root/.bashrc
fi

clear
printf "Welcome to \033[0;37mRezz\033[0;34mOS\033[0m!\n"
echo "System Info: rezzfetch"
echo "Packages:    pkg install <name> | pkg search <name> | pkg list"
echo "Type 'help' for available commands and shortcuts."
echo ""
