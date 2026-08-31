#!/bin/sh
export DISPLAY=:0

# Kill lingering daemons from previous sessions
killall pcmanfm 2>/dev/null || true

# Ensure dbus machine-id exists (needed for GTK apps)
mkdir -p /var/lib/dbus
dbus-uuidgen --ensure

# Start dbus if available
if [ -x /usr/bin/dbus-launch ]; then
    eval $(dbus-launch --sh-syntax --exit-with-session)
fi

# Configure keyboard layout from the saved keymap (rezzkeymap). The system
# xinitrc already reads this, but startx uses ~/.xinitrc, so apply it here too.
LAYOUT="us,ru"
TOGGLE="grp:alt_shift_toggle,grp_led:scroll"
if [ -f /etc/keymap.conf ]; then
    . /etc/keymap.conf
elif [ -f /mnt/disk/etc/keymap.conf ]; then
    . /mnt/disk/etc/keymap.conf
fi

if command -v setxkbmap >/dev/null 2>&1; then
    if [ -n "$TOGGLE" ]; then
        setxkbmap -layout "$LAYOUT" -option "$TOGGLE" 2>/dev/null || setxkbmap -layout "$LAYOUT" 2>/dev/null || true
    else
        setxkbmap -layout "$LAYOUT" -option "" 2>/dev/null || true
    fi
fi

# Enable desktop icons and wallpaper
pcmanfm --desktop &

exec jwm -f /root/.jwmrc
