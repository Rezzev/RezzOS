#!/bin/bash
cd "$(dirname "$0")"

FONT="TER16x32"
if [ -f "font.conf" ]; then
    FONT_VAL=$(grep -o 'FONT=[^ ]*' font.conf 2>/dev/null | cut -d= -f2)
    [ -n "$FONT_VAL" ] && FONT="$FONT_VAL"
fi

echo "Starting RezzOS in GUI mode (Font: $FONT)..."
qemu-system-x86_64 \
    -kernel bzImage \
    -initrd rootfs.cpio.gz \
    -append "vga=791 console=tty1 quiet loglevel=0 root=/dev/ram0 init=/init resume=/dev/vda fbcon=font:$FONT" \
    -netdev user,id=net0 -device virtio-net,netdev=net0 \
    -drive file=disk.img,format=raw,if=virtio \
    -m 512M \
    -vga std \
    -display gtk
