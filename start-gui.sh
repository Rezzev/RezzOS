#!/bin/bash
cd "$(dirname "$0")"
echo "Starting RezzOS in GUI mode..."
qemu-system-x86_64 \
    -kernel bzImage \
    -initrd rootfs.cpio.gz \
    -append "quiet loglevel=0 root=/dev/ram0 init=/init" \
    -netdev user,id=net0 -device virtio-net,netdev=net0 \
    -drive file=disk.img,format=raw,if=virtio \
    -m 512M \
    -vga std \
    -display gtk
