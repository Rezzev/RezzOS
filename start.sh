#!/bin/bash
cd "$(dirname "$0")"

FONT="TER16x32"
if [ -f "font.conf" ]; then
    FONT_VAL=$(grep -o 'FONT=[^ ]*' font.conf 2>/dev/null | cut -d= -f2)
    [ -n "$FONT_VAL" ] && FONT="$FONT_VAL"
fi

qemu-system-x86_64 \
    -kernel bzImage \
    -initrd rootfs.cpio.gz \
    -append "console=ttyS0 resume=/dev/vda fbcon=font:$FONT random.trust_cpu=on" \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 -device virtio-net,netdev=net0 \
    -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 \
    -drive file=disk.img,format=raw,if=virtio \
    -m 1024M -nographic
