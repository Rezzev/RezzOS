# Alpine Linux:
-------------
apk add build-base flex bison linux-headers elfutils-dev openssl-dev perl bc cpio gzip wget tar grub-bios xorriso qemu-system-x86_64 qemu-img

# Ubuntu/Debian:
--------------
apt install build-essential flex bison libncurses-dev libssl-dev libelf-dev bc cpio gzip wget tar grub-pc-bin xorriso qemu-system-x86 qemu-utils

# Arch Linux:
-----------
pacman -S base-devel flex bison bc openssl libelf cpio gzip wget tar grub xorriso qemu-desktop
