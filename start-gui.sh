#!/bin/bash
cd "$(dirname "$0")"

FONT="TER16x32"
if [ -f "font.conf" ]; then
    FONT_VAL=$(grep -o 'FONT=[^ ]*' font.conf 2>/dev/null | cut -d= -f2)
    [ -n "$FONT_VAL" ] && FONT="$FONT_VAL"
fi

EXTRA_DRIVE=""
if [ -f "target-disk.img" ]; then
    EXTRA_DRIVE="-drive file=target-disk.img,format=raw,if=virtio"
fi

echo "Starting RezzOS in GUI mode (Font: $FONT)..."
qemu-system-x86_64 \
    -kernel bzImage \
    -initrd rootfs.cpio.gz \
    -append "console=tty1 root=/dev/ram0 init=/init resume=/dev/vda fbcon=font:$FONT random.trust_cpu=on" \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 -device virtio-net,netdev=net0 \
    -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -drive file=disk.img,format=raw,if=virtio \
    $EXTRA_DRIVE \
    -m 2048M \
    -vga std \
    -display gtk
