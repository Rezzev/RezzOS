export TERM="${TERM:-linux}"
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
echo "Packages:"
echo "pkg install <name>"
echo "pkg search <name>"
echo "pkg list"
echo ""
