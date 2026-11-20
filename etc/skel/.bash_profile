# Подключаем интерактивные настройки (алиасы, приглашение командной строки, историю)
[ -f "$HOME/.bashrc" ] && . "$HOME/.bashrc"

# Раскомментируйте строку ниже для автозапуска X-сервера на первой консоли
# [[ -z $DISPLAY && $(tty 2>/dev/null) == /dev/tty1 ]] && exec startx

# Автозапуск tmux только при локальном интерактивном входе
if [ -z "$DISPLAY" ] && [ -z "$TMUX" ] && [ -t 0 ] && [ "$TERM" != "dumb" ] && [ -z "$SSH_CLIENT" ] && [ -z "$SSH_TTY" ] && command -v tmux >/dev/null 2>&1; then
    # Проверяем, смонтирована ли devpts, и монтируем с нужными правами для PTY
    if ! grep -qs '/dev/pts' /proc/mounts; then
        mkdir -p /dev/pts
        mount -t devpts devpts /dev/pts -o gid=5,mode=620,ptmxmode=0666 2>/dev/null || true
    fi

    # Убеждаемся, что /dev/ptmx корректно указывает на pts/ptmx (важно для initramfs/минимальных систем)
    if [ -e /dev/pts/ptmx ] && [ ! -L /dev/ptmx ]; then
        ln -sf /dev/pts/ptmx /dev/ptmx 2>/dev/null || true
    fi

    # Запускаем tmux только если есть доступ к управляющему терминалу (устраняет падение сервера tmux)
    if [ -c /dev/tty ] && (exec </dev/tty) 2>/dev/null; then
        exec tmux new-session -A -s rezzos
    fi
fi
