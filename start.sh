#!/bin/bash
cd /root/myos
qemu-system-x86_64 \
    -kernel bzImage \
    -initrd rootfs.cpio.gz \
    -append "console=ttyS0 quiet loglevel=0 root=/dev/ram0 init=/init" \
    -netdev user,id=net0 -device virtio-net,netdev=net0 \
    -drive file=disk.img,format=raw,if=virtio \
    -m 512M \
    -display sdl
