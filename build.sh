#!/bin/bash
# RezzOS Full Build Script

set -e

echo "=== Building RezzOS ==="

# 1. Build BusyBox
echo "Building BusyBox..."
cd busybox-1.36.1
make defconfig
sed -i 's/.*CONFIG_STATIC.*/CONFIG_STATIC=y/' .config
sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
yes "" | make oldconfig
make -j$(nproc)
make install
cd ..

# 2. Build kernel
echo "Building kernel..."
cd linux-6.6.40
make x86_64_defconfig
cat >> .config << 'KCONF'
CONFIG_VIRTIO_PCI=y
CONFIG_VIRTIO_BLK=y
CONFIG_VIRTIO_NET=y
CONFIG_VIRTIO_CONSOLE=y
CONFIG_DEVTMPFS=y
CONFIG_FB=y
CONFIG_FB_VESA=y
CONFIG_DRM=y
KCONF
yes "" | make oldconfig
make -j$(nproc)
cp arch/x86/boot/bzImage ../bzImage
cd ..

# 3. Create rootfs
echo "Creating rootfs..."
cp -r etc busybox-1.36.1/_install/
cp -r usr busybox-1.36.1/_install/
cp -r root busybox-1.36.1/_install/ 2>/dev/null
cp init busybox-1.36.1/_install/

cd busybox-1.36.1/_install
mkdir -p dev proc sys tmp mnt/disk var/log lib usr/lib usr/share/terminfo

# Download musl and libraries
echo "Downloading musl and libraries..."
cd /tmp
wget -q https://dl-cdn.alpinelinux.org/alpine/v3.20/main/x86_64/musl-1.2.5-r3.apk
mkdir -p /tmp/m && cd /tmp/m
tar -xzf /tmp/musl-1.2.5-r3.apk
cp lib/ld-musl-x86_64.so.1 ~/rezzos-repo/busybox-1.36.1/_install/lib/
cp -r lib/* ~/rezzos-repo/busybox-1.36.1/_install/lib/
cp -r usr/lib/* ~/rezzos-repo/busybox-1.36.1/_install/usr/lib/

# Essential packages
PACKAGES="ncurses-terminfo-base-6.4_p20240420-r2.apk libncursesw-6.4_p20240420-r2.apk readline-8.2.10-r0.apk bash-5.2.26-r0.apk nano-8.0-r0.apk"

for pkg in $PACKAGES; do
    wget -q "https://dl-cdn.alpinelinux.org/alpine/v3.20/main/x86_64/$pkg"
    mkdir -p /tmp/p && cd /tmp/p && rm -rf *
    tar -xzf "/tmp/$pkg" 2>/dev/null
    cp -r usr/* ~/rezzos-repo/busybox-1.36.1/_install/usr/ 2>/dev/null
    cp -r bin/* ~/rezzos-repo/busybox-1.36.1/_install/bin/ 2>/dev/null
    cp -r lib/* ~/rezzos-repo/busybox-1.36.1/_install/lib/ 2>/dev/null
    cp -r etc/* ~/rezzos-repo/busybox-1.36.1/_install/etc/ 2>/dev/null
done

# Create rootfs image
cd ~/rezzos-repo/busybox-1.36.1/_install
find . | cpio -o -H newc | gzip > ../../rootfs.cpio.gz
cd ~/rezzos-repo

echo ""
echo "=== Build complete ==="
echo "Files:"
ls -lh bzImage rootfs.cpio.gz
echo ""
echo "Run: ./start.sh"
