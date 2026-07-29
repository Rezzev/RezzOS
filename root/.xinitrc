#!/bin/sh
export DISPLAY=:0

# Ensure dbus machine-id exists (needed for GTK apps)
mkdir -p /var/lib/dbus
dbus-uuidgen --ensure

# Start dbus if available
if [ -x /usr/bin/dbus-launch ]; then
    eval $(dbus-launch --sh-syntax --exit-with-session)
fi

# Disable mousepad root warning
mkdir -p /root/.config/dconf
gsettings set org.xfce.mousepad.preferences.window show-root-warning false 2>/dev/null &

# Enable desktop icons and wallpaper
pcmanfm --desktop &

exec jwm -f /root/.jwmrc
