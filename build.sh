#!/bin/bash
set -e

KERNEL_VERSION="6.6.40"
BUSYBOX_VERSION="1.36.1"
ALPINE_MAIN="https://dl-cdn.alpinelinux.org/alpine/v3.20/main/x86_64"
ALPINE_COMMUNITY="https://dl-cdn.alpinelinux.org/alpine/v3.20/community/x86_64"

# resolve repo root from the script's own location instead of assuming
# it lives at some fixed path on someone's machine
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="$REPO_DIR/linux-${KERNEL_VERSION}"
BUSYBOX_DIR="$REPO_DIR/busybox-${BUSYBOX_VERSION}"
ROOTFS_DIR="$BUSYBOX_DIR/_install"

JOBS="$(nproc)"

log() {
    echo -e "\n\033[1;32m==> $1\033[0m"
}

# quick sanity check so this doesn't silently do the wrong thing if
# someone runs it from outside the repo
if [ ! -f "$REPO_DIR/init" ] || [ ! -d "$REPO_DIR/etc" ] || [ ! -d "$REPO_DIR/usr" ]; then
    echo "Run this script from the root of a cloned RezzOS repository."
    exit 1
fi

for cmd in wget tar make gcc cpio gzip qemu-img mkfs.ext4; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing command: $cmd"
        exit 1
    fi
done

cd "$REPO_DIR"

# grab busybox source if it's not already there, don't assume it's
# been manually downloaded first
if [ ! -d "$BUSYBOX_DIR" ]; then
    log "Downloading BusyBox ${BUSYBOX_VERSION}"
    wget -q "https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2" -O "busybox-${BUSYBOX_VERSION}.tar.bz2"
    tar -xjf "busybox-${BUSYBOX_VERSION}.tar.bz2"
fi

log "Building BusyBox"
cd "$BUSYBOX_DIR"
make defconfig
sed -i 's/.*CONFIG_STATIC.*/CONFIG_STATIC=y/' .config
sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
yes "" | make oldconfig
make -j"$JOBS"
make install
cd "$REPO_DIR"

# same deal for the kernel source
if [ ! -d "$KERNEL_DIR" ]; then
    log "Downloading kernel ${KERNEL_VERSION}"
    wget -q "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.xz" -O "linux-${KERNEL_VERSION}.tar.xz"
    tar -xf "linux-${KERNEL_VERSION}.tar.xz"
fi

log "Building kernel"
cd "$KERNEL_DIR"
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
make -j"$JOBS"
cp arch/x86/boot/bzImage "$REPO_DIR/bzImage"
cd "$REPO_DIR"

log "Assembling rootfs"
cp -r etc "$ROOTFS_DIR/"
cp -r usr "$ROOTFS_DIR/"
[ -d root ] && cp -r root "$ROOTFS_DIR/" || true
cp init "$ROOTFS_DIR/"

cd "$ROOTFS_DIR"
mkdir -p dev proc sys tmp mnt/disk var/log lib usr/lib usr/share/terminfo
# init ends with `exec runsvdir /etc/runit/runsvdir/default` but nothing
# was creating this dir, so runit had nothing to supervise
mkdir -p etc/runit/runsvdir/default
chmod +x init

log "Downloading musl"
mkdir -p /tmp/m
cd /tmp
wget -q "$ALPINE_MAIN/musl-1.2.5-r3.apk"
cd /tmp/m
rm -rf ./*
tar -xzf /tmp/musl-1.2.5-r3.apk
cp lib/ld-musl-x86_64.so.1 "$ROOTFS_DIR/lib/"
cp -r lib/* "$ROOTFS_DIR/lib/"
# the musl apk doesn't actually ship a usr/lib dir, the unconditional
# cp for it was the thing killing the script under set -e
[ -d usr/lib ] && cp -r usr/lib/* "$ROOTFS_DIR/usr/lib/" || true

log "Downloading Alpine packages"
MAIN_PACKAGES="ncurses-terminfo-base-6.4_p20240420-r2.apk libncursesw-6.4_p20240420-r2.apk readline-8.2.10-r0.apk bash-5.2.26-r0.apk nano-8.0-r0.apk dropbear-2024.85-r0.apk dropbear-ssh-2024.85-r0.apk"
for pkg in $MAIN_PACKAGES; do
    wget -q "$ALPINE_MAIN/$pkg" -O "/tmp/$pkg"
    mkdir -p /tmp/p
    cd /tmp/p
    rm -rf ./*
    tar -xzf "/tmp/$pkg" 2>/dev/null
    [ -d usr ] && cp -r usr/* "$ROOTFS_DIR/usr/" 2>/dev/null || true
    [ -d bin ] && cp -r bin/* "$ROOTFS_DIR/bin/" 2>/dev/null || true
    [ -d lib ] && cp -r lib/* "$ROOTFS_DIR/lib/" 2>/dev/null || true
    [ -d etc ] && cp -r etc/* "$ROOTFS_DIR/etc/" 2>/dev/null || true
done

# runit isn't in the main repo, it's in community - this was missing
# entirely before, which is why runsvdir was never actually present
COMMUNITY_PACKAGES="runit-2.1.2-r7.apk"
for pkg in $COMMUNITY_PACKAGES; do
    wget -q "$ALPINE_COMMUNITY/$pkg" -O "/tmp/$pkg"
    mkdir -p /tmp/p
    cd /tmp/p
    rm -rf ./*
    tar -xzf "/tmp/$pkg" 2>/dev/null
    [ -d usr ] && cp -r usr/* "$ROOTFS_DIR/usr/" 2>/dev/null || true
    [ -d sbin ] && cp -r sbin/* "$ROOTFS_DIR/sbin/" 2>/dev/null || true
    [ -d bin ] && cp -r bin/* "$ROOTFS_DIR/bin/" 2>/dev/null || true
    [ -d lib ] && cp -r lib/* "$ROOTFS_DIR/lib/" 2>/dev/null || true
    [ -d etc ] && cp -r etc/* "$ROOTFS_DIR/etc/" 2>/dev/null || true
done

log "Packing rootfs.cpio.gz"
cd "$ROOTFS_DIR"
find . | cpio -o -H newc | gzip > "$REPO_DIR/rootfs.cpio.gz"
cd "$REPO_DIR"

# nothing in the repo actually creates disk.img, but start.sh expects
# one to already exist
if [ ! -f "$REPO_DIR/disk.img" ]; then
    log "Creating disk.img"
    qemu-img create -f raw disk.img 256M
    mkfs.ext4 -F disk.img
fi

log "Done"
ls -la bzImage rootfs.cpio.gz disk.img

if [ -f "$REPO_DIR/start.sh" ]; then
    echo "Run: ./start.sh"
else
    cat << 'EOF'

Run:
    qemu-system-x86_64 \
        -kernel bzImage \
        -initrd rootfs.cpio.gz \
        -append "console=ttyS0" \
        -netdev user,id=net0 -device virtio-net,netdev=net0 \
        -drive file=disk.img,format=raw,if=virtio \
        -m 512M -nographic
EOF
fi
