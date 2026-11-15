# Если оболочка запущена не в интерактивном режиме, прекращаем выполнение скрипта
case $- in
    *i*) ;;
      *) return;;
esac

export TERM="${TERM:-linux}"
export INPUTRC=/etc/inputrc
export HISTFILE=/root/.bash_history
export HISTSIZE=10000
export HISTFILESIZE=10000

shopt -s histappend 2>/dev/null || true
PROMPT_COMMAND="history -a 2>/dev/null${PROMPT_COMMAND:+; $PROMPT_COMMAND}"

PS1='\[\e[1;34m\]\u\[\e[0m\]@\h:\w\$> '

alias ll='ls -la'
alias ..='cd ..'
