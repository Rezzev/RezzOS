# RezzOS Build Guide

Follow this guide to build RezzOS on your PC.

## Prerequisites

First, clone the repository:

```bash
git clone https://github.com/Rezzev/RezzOS.git
```

Then install the required build dependencies:

```bash
apk add build-base flex bison linux-headers elfutils-dev openssl-dev perl bc cpio gzip wget tar grub-bios xorriso qemu-system-x86_64 qemu-img fakeroot gtk+3.0-dev gtksourceview-dev webkit2gtk-4.1
```

> Important: You need Alpine Linux installed either in Docker or directly on your machine.
>
> Otherwise, the required tools may not compile correctly under musl.
>
> You may try building on another musl-based distribution, but Alpine is strongly recommended.

## Build

Run the build as root or with sudo/doas:

```bash
sudo ./build.sh
# or
#doas ./build.sh
```

The build process will start and progress through the following stages:

```
BusyBox -> Kernel -> Build Programs -> Create rootfs -> Create ISO
```

After the build completes successfully, you will have a ready-to-use ISO image.
