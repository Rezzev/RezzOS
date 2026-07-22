#!/bin/bash
# Stop the build immediately on any error.
set -e

# Treat unset variables as errors.
set -u

KERNEL_VERSION="6.6.40"
BUSYBOX_VERSION="1.36.1"
ALPINE_MAIN="https://dl-cdn.alpinelinux.org/alpine/v3.20/main/x86_64"
ALPINE_COMMUNITY="https://dl-cdn.alpinelinux.org/alpine/v3.20/community/x86_64"
ALPINE_EDGE_COMMUNITY="https://dl-cdn.alpinelinux.org/alpine/edge/community/x86_64"
ALPINE_EDGE_MAIN="https://dl-cdn.alpinelinux.org/alpine/edge/main/x86_64"

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="$REPO_DIR/linux-${KERNEL_VERSION}"
BUSYBOX_DIR="$REPO_DIR/busybox-${BUSYBOX_VERSION}"
ROOTFS_DIR="$BUSYBOX_DIR/_install"
CACHE_DIR="$REPO_DIR/dl_cache"

JOBS="$(nproc)"

TOTAL_STEPS=8
CURRENT_STEP=0

step() {
    CURRENT_STEP=$((CURRENT_STEP + 1))
    echo ""
    echo "[$CURRENT_STEP/$TOTAL_STEPS] $1"
    echo "============================================"
}
log() {
    echo -e "\n\033[1;32m==> $1\033[0m"
}

mkdir -p "$CACHE_DIR"

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

# Download BusyBox source if not present
if [ ! -d "$BUSYBOX_DIR" ]; then
    step "Downloading BusyBox ${BUSYBOX_VERSION}"
    if [ ! -f "$CACHE_DIR/busybox-${BUSYBOX_VERSION}.tar.bz2" ]; then
        wget -q "https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2" -O "$CACHE_DIR/busybox-${BUSYBOX_VERSION}.tar.bz2"
    fi
    tar -xjf "$CACHE_DIR/busybox-${BUSYBOX_VERSION}.tar.bz2"
fi

step "Building BusyBox"
cd "$BUSYBOX_DIR"
if [ ! -f "busybox" ]; then
    make defconfig
    sed -i 's/.*CONFIG_STATIC.*/CONFIG_STATIC=y/' .config
    sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
    sed -i 's/CONFIG_FEATURE_USAGE=y/# CONFIG_FEATURE_USAGE is not set/' .config
    sed -i 's/CONFIG_FEATURE_VERBOSE_USAGE=y/# CONFIG_FEATURE_VERBOSE_USAGE is not set/' .config
    sed -i 's/CONFIG_FEATURE_COMPRESS_USAGE=y/# CONFIG_FEATURE_COMPRESS_USAGE is not set/' .config
    sed -i 's/CONFIG_VI=y/# CONFIG_VI is not set/' .config
    sed -i 's/CONFIG_FEATURE_VI_.*/# & is not set/' .config
    yes "" | make oldconfig
    make -j"$JOBS"
fi
make install
cd "$REPO_DIR"

# Smart Kernel Caching: Skip kernel build if bzImage exists unless FORCE_REBUILD_KERNEL=1
if [ -f "$REPO_DIR/bzImage" ] && [ "${FORCE_REBUILD_KERNEL:-0}" != "1" ]; then
    log "Using existing bzImage kernel. (Set FORCE_REBUILD_KERNEL=1 to rebuild)"
else
    if [ ! -d "$KERNEL_DIR" ]; then
        step "Downloading kernel ${KERNEL_VERSION}"
        if [ ! -f "$CACHE_DIR/linux-${KERNEL_VERSION}.tar.xz" ]; then
            wget -q "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.xz" -O "$CACHE_DIR/linux-${KERNEL_VERSION}.tar.xz"
        fi
        tar -xf "$CACHE_DIR/linux-${KERNEL_VERSION}.tar.xz"
    fi

    step "Building kernel"
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
CONFIG_UNIX98_PTYS=y
CONFIG_DEVPTS_MULTIPLE_INSTANCES=y
KCONF
    yes "" | make oldconfig
    make -j"$JOBS"
    cp arch/x86/boot/bzImage "$REPO_DIR/bzImage"
    cd "$REPO_DIR"
fi

step "Assembling rootfs"
cp -r etc "$ROOTFS_DIR/"
cp -r usr "$ROOTFS_DIR/"
[ -d root ] && cp -r root "$ROOTFS_DIR/" || true
cp init "$ROOTFS_DIR/"

cd "$ROOTFS_DIR"
mkdir -p dev proc sys tmp mnt/disk var/log lib usr/lib usr/share/terminfo
mkdir -p etc/runit/runsvdir/default
chmod +x init

step "Downloading musl"
if [ ! -f "$CACHE_DIR/musl-1.2.5-r3.apk" ]; then
    wget -q "$ALPINE_MAIN/musl-1.2.5-r3.apk" -O "$CACHE_DIR/musl-1.2.5-r3.apk"
fi
mkdir -p /tmp/m
cd /tmp/m
rm -rf ./*
tar -xzf "$CACHE_DIR/musl-1.2.5-r3.apk"
cp lib/ld-musl-x86_64.so.1 "$ROOTFS_DIR/lib/"
cp -r lib/* "$ROOTFS_DIR/lib/"
[ -d usr/lib ] && cp -r usr/lib/* "$ROOTFS_DIR/usr/lib/" || true

step "Downloading Alpine packages (using local cache)"
MAIN_PACKAGES="ncurses-terminfo-base-6.4_p20240420-r2.apk libncursesw-6.4_p20240420-r2.apk libevent-2.1.12-r7.apk tmux-3.4-r1.apk readline-8.2.10-r0.apk bash-5.2.26-r0.apk nano-8.0-r0.apk dropbear-2024.85-r0.apk dropbear-ssh-2024.85-r0.apk zlib-1.3.2-r0.apk musl-dev-1.2.5-r3.apk musl-1.2.5-r3.apk make-4.4.1-r2.apk lua5.3-5.3.6-r6.apk lua5.3-libs-5.3.6-r6.apk linenoise-1.0-r5.apk"
for pkg in $MAIN_PACKAGES; do
    if [ ! -f "$CACHE_DIR/$pkg" ]; then
        wget -q "$ALPINE_MAIN/$pkg" -O "$CACHE_DIR/$pkg"
    fi
    mkdir -p /tmp/p
    cd /tmp/p
    rm -rf ./*
    tar -xzf "$CACHE_DIR/$pkg" 2>/dev/null
    [ -d usr ] && cp -r usr/* "$ROOTFS_DIR/usr/" 2>/dev/null || true
    [ -d bin ] && cp -r bin/* "$ROOTFS_DIR/bin/" 2>/dev/null || true
    [ -d lib ] && cp -r lib/* "$ROOTFS_DIR/lib/" 2>/dev/null || true
    [ -d etc ] && cp -r etc/* "$ROOTFS_DIR/etc/" 2>/dev/null || true
done

COMMUNITY_PACKAGES="runit-2.1.2-r7.apk neatvi-15-r0.apk"
for pkg in $COMMUNITY_PACKAGES; do
    if [ ! -f "$CACHE_DIR/$pkg" ]; then
        wget -q "$ALPINE_COMMUNITY/$pkg" -O "$CACHE_DIR/$pkg"
    fi
    mkdir -p /tmp/p
    cd /tmp/p
    rm -rf ./*
    tar -xzf "$CACHE_DIR/$pkg" 2>/dev/null
    [ -d usr ] && cp -r usr/* "$ROOTFS_DIR/usr/" 2>/dev/null || true
    [ -d sbin ] && cp -r sbin/* "$ROOTFS_DIR/sbin/" 2>/dev/null || true
    [ -d bin ] && cp -r bin/* "$ROOTFS_DIR/bin/" 2>/dev/null || true
    [ -d lib ] && cp -r lib/* "$ROOTFS_DIR/lib/" 2>/dev/null || true
    [ -d etc ] && cp -r etc/* "$ROOTFS_DIR/etc/" 2>/dev/null || true
done

EDGE_COMMUNITY_PACKAGES="tcc-0.9.27_git20250619-r1.apk tcc-libs-0.9.27_git20250619-r1.apk tcc-libs-static-0.9.27_git20250619-r1.apk"
for pkg in $EDGE_COMMUNITY_PACKAGES; do
    if [ ! -f "$CACHE_DIR/$pkg" ]; then
        wget -q "$ALPINE_EDGE_COMMUNITY/$pkg" -O "$CACHE_DIR/$pkg"
    fi
    mkdir -p /tmp/p
    cd /tmp/p
    rm -rf ./*
    tar -xzf "$CACHE_DIR/$pkg" 2>/dev/null
    [ -d usr ] && cp -r usr/* "$ROOTFS_DIR/usr/" 2>/dev/null || true
    [ -d sbin ] && cp -r sbin/* "$ROOTFS_DIR/sbin/" 2>/dev/null || true
    [ -d bin ] && cp -r bin/* "$ROOTFS_DIR/bin/" 2>/dev/null || true
    [ -d lib ] && cp -r lib/* "$ROOTFS_DIR/lib/" 2>/dev/null || true
    [ -d etc ] && cp -r etc/* "$ROOTFS_DIR/etc/" 2>/dev/null || true
done

chmod +x "$ROOTFS_DIR/usr/bin/"* 2>/dev/null || true

step "Packing rootfs.cpio.gz"
cd "$ROOTFS_DIR"
find . | cpio -o -H newc | gzip > "$REPO_DIR/rootfs.cpio.gz"
cd "$REPO_DIR"

# create disk.img
if [ ! -f "$REPO_DIR/disk.img" ]; then
    step "Creating disk.img"
    qemu-img create -f raw disk.img 256M
    mkfs.ext4 -F disk.img
fi

step "Done"
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
