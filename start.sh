#!/bin/bash
cd "$(dirname "$0")"
qemu-system-x86_64 \
    -kernel bzImage \
    -initrd rootfs.cpio.gz \
    -append "console=ttyS0" \
    -netdev user,id=net0 -device virtio-net,netdev=net0 \
    -drive file=disk.img,format=raw,if=virtio \
    -m 512M -nographic
