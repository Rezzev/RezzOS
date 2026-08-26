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

TOTAL_STEPS=9
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

if [ ! -f "$REPO_DIR/init" ] || [ ! -d "$REPO_DIR/etc" ] || [ ! -d "$REPO_DIR/usr" ] || [ ! -f "$REPO_DIR/kernel.config" ]; then
    echo "Run this script from the root of a cloned RezzOS repository."
    exit 1
fi

for cmd in wget tar make gcc cpio gzip qemu-img mkfs.ext4 fakeroot; do
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
    sed -i 's/CONFIG_VI=.*/CONFIG_VI=y/' .config
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
    cp "$REPO_DIR/kernel.config" .config
    cat >> .config << 'KCONF'
CONFIG_VIRTIO_PCI=y
CONFIG_VIRTIO_BLK=y
CONFIG_VIRTIO_NET=y
CONFIG_VIRTIO_CONSOLE=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_I2C=y
CONFIG_AGP=y
CONFIG_AGP_AMD64=y
CONFIG_AGP_INTEL=y
CONFIG_DRM=y
CONFIG_DRM_KMS_HELPER=y
CONFIG_DRM_DISPLAY_HELPER=y
CONFIG_DRM_BOCHS=y
CONFIG_DRM_VIRTIO_GPU=y
CONFIG_DRM_SIMPLEDRM=y
CONFIG_SYSFB_SIMPLEFB=y
CONFIG_FB=y
CONFIG_FB_VESA=y
CONFIG_FB_SIMPLE=y
CONFIG_FB_EFI=y
CONFIG_FRAMEBUFFER_CONSOLE=y
CONFIG_FRAMEBUFFER_CONSOLE_DETECT_PRIMARY=y
CONFIG_UNIX98_PTYS=y
CONFIG_DEVPTS_MULTIPLE_INSTANCES=y
CONFIG_PM=y
CONFIG_PM_SLEEP=y
CONFIG_SUSPEND=y
CONFIG_HIBERNATION=y
CONFIG_HIBERNATION_SNAPSHOT_DEV=y
CONFIG_ACPI=y
CONFIG_ACPI_SYSTEM_POWER_STATES_SUPPORT=y
CONFIG_FONT_SUPPORT=y
# CONFIG_FONT_AUTOSELECT is not set
CONFIG_FONT_8x16=y
CONFIG_FONT_8x8=y
CONFIG_FONT_6x11=y
CONFIG_FONT_7x14=y
CONFIG_FONT_PEARL_8x8=y
CONFIG_FONT_ACORN_8x8=y
CONFIG_FONT_MINI_4x6=y
CONFIG_FONT_6x10=y
CONFIG_FONT_10x18=y
CONFIG_FONT_SUN8x16=y
CONFIG_FONT_SUN12x22=y
CONFIG_FONT_TER16x32=y
CONFIG_FONT_6x8=y
KCONF
    make olddefconfig
    make -j"$JOBS"
    cp arch/x86/boot/bzImage "$REPO_DIR/bzImage"
    cd "$REPO_DIR"
fi

step "Assembling rootfs"
rm -f "$ROOTFS_DIR/usr/bin/font" "$ROOTFS_DIR/usr/bin/rezzfont" "$ROOTFS_DIR/bin/font" "$ROOTFS_DIR/bin/rezzfont" 2>/dev/null || true
[ -d "$ROOTFS_DIR/etc" ] && chmod -R u+w "$ROOTFS_DIR/etc" 2>/dev/null || true
cp -rf --remove-destination etc "$ROOTFS_DIR/"
cp -rf --remove-destination usr "$ROOTFS_DIR/"
[ -d root ] && cp -rf --remove-destination root "$ROOTFS_DIR/" || true
cp -f init "$ROOTFS_DIR/"

cd "$ROOTFS_DIR"
mkdir -p dev proc sys tmp mnt/disk var/log lib usr/lib usr/share/terminfo
mkdir -p etc/runit/runsvdir/default
mkdir -p boot
rm -f boot/rootfs.cpio.gz
cp "$REPO_DIR/bzImage" boot/ 2>/dev/null || true
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
MAIN_GUI="libsm-1.2.4-r4.apk xz-libs-5.8.3-r0.apk dbus-x11-1.14.10-r1.apk dbus-libs-1.14.10-r1.apk libpng-1.6.57-r0.apk libffi-3.4.6-r0.apk mesa-24.0.9-r1.apk harfbuzz-8.5.0-r0.apk wayland-libs-client-1.22.0-r4.apk libgcc-13.2.1_git20240309-r1.apk pcre2-10.43-r0.apk libuuid-2.40.1-r1.apk libice-1.1.1-r6.apk libxdmcp-1.1.5-r1.apk libxt-1.3.0-r5.apk libxxf86vm-1.1.5-r6.apk libxau-1.0.11-r4.apk pango-1.52.2-r0.apk mcookie-2.40.1-r1.apk libxft-2.3.8-r3.apk libeconf-0.6.3-r0.apk libxinerama-1.1.5-r4.apk libpciaccess-0.18.1-r0.apk libxshmfence-1.3.2-r6.apk cairo-1.18.4-r0.apk font-alias-1.0.5-r0.apk brotli-libs-1.1.0-r2.apk font-terminus-4.49.1-r4.apk libmd-1.1.0-r0.apk encodings-1.0.7-r1.apk pixman-0.43.2-r0.apk libblkid-2.40.1-r1.apk font-liberation-2.1.5-r2.apk musl-1.2.5-r3.apk wayland-libs-server-1.22.0-r4.apk libxcb-1.16.1-r0.apk font-dejavu-2.37-r5.apk mkfontscale-1.2.2-r6.apk font-misc-misc-1.1.3-r1.apk libcrypto3-3.3.7-r0.apk udev-init-scripts-35-r1.apk graphite2-1.3.14-r6.apk libstdc++-13.2.1_git20240309-r1.apk libmount-2.40.1-r1.apk libdrm-2.4.120-r0.apk xkeyboard-config-2.41-r0.apk fribidi-1.0.15-r0.apk mesa-gbm-24.0.9-r1.apk mesa-gl-24.0.9-r1.apk hwdata-pci-0.382-r0.apk fontconfig-2.15.0-r1.apk kmod-libs-32-r0.apk freetype-2.13.2-r0.apk nettle-3.10.2-r0.apk libxmu-1.1.4-r2.apk util-macros-1.20.0-r0.apk libbsd-0.12.2-r0.apk libx11-1.8.9-r1.apk libepoxy-1.5.10-r1.apk mesa-glapi-24.0.9-r1.apk zlib-1.3.2-r0.apk tiff-4.6.0t-r0.apk dbus-1.14.10-r1.apk eudev-3.2.14-r2.apk xkbcomp-1.5.0-r0.apk libsharpyuv-1.3.2-r0.apk libxrender-0.9.11-r5.apk libfontenc-1.1.8-r0.apk mesa-egl-24.0.9-r1.apk libjpeg-turbo-3.0.3-r0.apk libxext-1.3.6-r2.apk gdk-pixbuf-2.42.12-r0.apk libbz2-1.0.8-r6.apk libxfixes-6.0.1-r4.apk libintl-0.22.5-r0.apk ncurses-terminfo-base-6.4_p20240420-r2.apk libexpat-2.8.1-r0.apk eudev-libs-3.2.14-r2.apk gmp-6.3.0-r1.apk glib-2.80.5-r0.apk libwebp-1.3.2-r0.apk libxpm-3.5.19-r0.apk libxrandr-1.5.4-r1.apk libxml2-2.12.10-r0.apk zstd-libs-1.5.6-r0.apk cairo-gobject-1.18.4-r0.apk shared-mime-info-2.4-r0.apk libxkbfile-1.1.3-r0.apk font-cursor-misc-1.0.4-r1.apk"
MAIN_PACKAGES="libeconf-0.6.3-r0.apk zstd-libs-1.5.6-r0.apk xz-libs-5.8.3-r0.apk ncurses-terminfo-base-6.4_p20240420-r2.apk dhcpcd-10.0.6-r1.apk libncursesw-6.4_p20240420-r2.apk libevent-2.1.12-r7.apk tmux-3.4-r1.apk readline-8.2.10-r0.apk bash-5.2.26-r0.apk nano-8.0-r0.apk dropbear-2024.85-r0.apk dropbear-ssh-2024.85-r0.apk zlib-1.3.2-r0.apk musl-dev-1.2.5-r3.apk musl-1.2.5-r3.apk make-4.4.1-r2.apk lua5.3-5.3.6-r6.apk lua5.3-libs-5.3.6-r6.apk linenoise-1.0-r5.apk syslinux-6.04_pre1-r15.apk e2fsprogs-1.47.0-r5.apk e2fsprogs-libs-1.47.0-r5.apk libcom_err-1.47.0-r5.apk dosfstools-4.2-r2.apk"
for pkg in $MAIN_PACKAGES $MAIN_GUI; do
    if [ ! -s "$CACHE_DIR/$pkg" ]; then
        rm -f "$CACHE_DIR/$pkg"
        wget "$ALPINE_MAIN/$pkg" -O "$CACHE_DIR/$pkg" || { echo "Failed to download $pkg from $ALPINE_MAIN"; exit 1; }
    fi
    mkdir -p /tmp/p
    cd /tmp/p
    rm -rf ./*
    tar -xzf "$CACHE_DIR/$pkg" 2>/dev/null
    [ -d usr ] && cp -r usr/* "$ROOTFS_DIR/usr/" 2>/dev/null || true
    [ -d bin ] && cp -r bin/* "$ROOTFS_DIR/bin/" 2>/dev/null || true
    [ -d sbin ] && cp -r sbin/* "$ROOTFS_DIR/sbin/" 2>/dev/null || true
    [ -d lib ] && cp -r lib/* "$ROOTFS_DIR/lib/" 2>/dev/null || true
    [ -d etc ] && cp -r etc/* "$ROOTFS_DIR/etc/" 2>/dev/null || true
    [ -d var ] && cp -r var/* "$ROOTFS_DIR/var/" 2>/dev/null || true
    [ -d share ] && cp -r share/* "$ROOTFS_DIR/usr/share/" 2>/dev/null || true
done

COMM_GUI="mtdev-1.1.6-r3.apk libinput-libs-1.25.0-r0.apk xorg-server-common-21.1.14-r0.apk xrdb-1.2.2-r0.apk jwm-2.4.3-r0.apk libevdev-1.13.1-r0.apk setxkbmap-1.3.4-r0.apk st-0.9.2-r0.apk xinit-1.4.2-r1.apk libxcvt-0.1.2-r0.apk xmodmap-1.0.11-r1.apk xorg-server-21.1.14-r0.apk xf86-video-fbdev-0.5.0-r6.apk xf86-video-vesa-2.6.0-r4.apk librsvg-2.58.5-r0.apk xauth-1.1.3-r0.apk xf86-input-libinput-1.4.0-r1.apk libxfont2-2.0.6-r4.apk"
COMMUNITY_PACKAGES="runit-2.1.2-r7.apk neatvi-15-r0.apk sudo-1.9.15_p5-r0.apk iwd-2.17-r0.apk exfatprogs-1.2.2-r0.apk "
for pkg in $COMMUNITY_PACKAGES $COMM_GUI; do
    if [ ! -s "$CACHE_DIR/$pkg" ]; then
        rm -f "$CACHE_DIR/$pkg"
        wget "$ALPINE_COMMUNITY/$pkg" -O "$CACHE_DIR/$pkg" || { echo "Failed to download $pkg from $ALPINE_COMMUNITY"; exit 1; }
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
    [ -d var ] && cp -r var/* "$ROOTFS_DIR/var/" 2>/dev/null || true
    [ -d share ] && cp -r share/* "$ROOTFS_DIR/usr/share/" 2>/dev/null || true
done

EDGE_COMMUNITY_PACKAGES="links-graphics-2.30-r1.apk tcc-0.9.27_git20260714-r0.apk tcc-libs-0.9.27_git20260714-r0.apk tcc-libs-static-0.9.27_git20260714-r0.apk"
for pkg in $EDGE_COMMUNITY_PACKAGES; do
    if [ ! -s "$CACHE_DIR/$pkg" ]; then
        rm -f "$CACHE_DIR/$pkg"
        wget "$ALPINE_EDGE_COMMUNITY/$pkg" -O "$CACHE_DIR/$pkg" || { echo "Failed to download $pkg from $ALPINE_EDGE_COMMUNITY"; exit 1; }
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

# Wi-Fi firmware, pinned to exact versions (Depends: None on each, so the
# linux-firmware metapackage with every blob in existence is not pulled in).
#
# Intel Wi-Fi blobs live in the "uncategorized" linux-firmware-other package
# (81 MB apk, 205 MB installed - audio, GPUs, everything nobody shipped a
# dedicated subpackage for), so it gets a selective copy of iwlwifi* instead
# of a full unpack. Blobs ship as *.zst - CONFIG_FW_LOADER_COMPRESS_ZSTD in
# kernel.config makes the kernel able to load them.
FIRMWARE_PACKAGES="linux-firmware-ath10k-20240811-r0.apk linux-firmware-rtlwifi-20240811-r0.apk"
for pkg in $FIRMWARE_PACKAGES; do
    if [ ! -s "$CACHE_DIR/$pkg" ]; then
        rm -f "$CACHE_DIR/$pkg"
        wget "$ALPINE_MAIN/$pkg" -O "$CACHE_DIR/$pkg" || { echo "Failed to download $pkg from $ALPINE_MAIN"; exit 1; }
    fi
    mkdir -p /tmp/p
    cd /tmp/p
    rm -rf ./*
    tar -xzf "$CACHE_DIR/$pkg"
    [ -d lib ] && cp -r lib/* "$ROOTFS_DIR/lib/" 2>/dev/null || true
done

if [ ! -s "$CACHE_DIR/linux-firmware-other-20240811-r0.apk" ]; then
    rm -f "$CACHE_DIR/linux-firmware-other-20240811-r0.apk"
    wget "$ALPINE_MAIN/linux-firmware-other-20240811-r0.apk" -O "$CACHE_DIR/linux-firmware-other-20240811-r0.apk" || { echo "Failed to download linux-firmware-other"; exit 1; }
fi
rm -rf /tmp/p && mkdir -p /tmp/p && cd /tmp/p
tar -xzf "$CACHE_DIR/linux-firmware-other-20240811-r0.apk"
cp lib/firmware/iwlwifi-* "$ROOTFS_DIR/lib/firmware/" 2>/dev/null || true
cd "$REPO_DIR"

manifest=/tmp/p/iwl-manifest
: > "$manifest"
for f in "$ROOTFS_DIR"/lib/firmware/iwlwifi-*.ucode; do
    [ -e "$f" ] || continue
    b=${f##*/}; b=${b%.ucode}
    case ${b##*-} in ''|*[!0-9]*) continue ;; esac
    printf '%s %s %s\n' "${b%-*}" "${b##*-}" "$f" >> "$manifest"
done
LC_ALL=C sort -k1,1 -k2,2rn "$manifest" | awk 'c[$1]++ >= 2 {print $3}' > /tmp/p/iwl-drop
if [ -s /tmp/p/iwl-drop ]; then
    xargs rm -f < /tmp/p/iwl-drop
    echo "Firmware diet: dropped $(wc -l < /tmp/p/iwl-drop) old iwlwifi revisions"
fi
rm -rf "$ROOTFS_DIR/lib/firmware/ath10k/WCN3990/hw1.0/qcm2290"

step "Installing GUI apps and dependencies using apk.static"
if [ ! -s "$CACHE_DIR/apk-tools-static.apk" ]; then
    wget -q "http://dl-cdn.alpinelinux.org/alpine/v3.20/main/x86_64/apk-tools-static-2.14.4-r1.apk" -O "$CACHE_DIR/apk-tools-static.apk" || true
fi
tar -xzf "$CACHE_DIR/apk-tools-static.apk" sbin/apk.static 2>/dev/null || true

# Make sure directories are writable before apk.static installs into them
[ -d "$ROOTFS_DIR/etc" ] && chmod -R u+w "$ROOTFS_DIR/etc" 2>/dev/null || true
[ -d "$ROOTFS_DIR/usr" ] && chmod -R u+w "$ROOTFS_DIR/usr" 2>/dev/null || true
[ -d "$ROOTFS_DIR/lib" ] && chmod -R u+w "$ROOTFS_DIR/lib" 2>/dev/null || true
[ -d "$ROOTFS_DIR/bin" ] && chmod -R u+w "$ROOTFS_DIR/bin" 2>/dev/null || true
[ -d "$ROOTFS_DIR/sbin" ] && chmod -R u+w "$ROOTFS_DIR/sbin" 2>/dev/null || true

if [ -f sbin/apk.static ]; then
    mkdir -p "$ROOTFS_DIR/etc/apk" "$ROOTFS_DIR/lib/apk/db" "$ROOTFS_DIR/var/cache/apk"
    touch "$ROOTFS_DIR/etc/apk/world"
    fakeroot ./sbin/apk.static --root "$ROOTFS_DIR" --initdb -X http://dl-cdn.alpinelinux.org/alpine/v3.20/main -X http://dl-cdn.alpinelinux.org/alpine/v3.20/community --allow-untrusted update
    fakeroot ./sbin/apk.static --root "$ROOTFS_DIR" -X http://dl-cdn.alpinelinux.org/alpine/v3.20/main -X http://dl-cdn.alpinelinux.org/alpine/v3.20/community --allow-untrusted --no-scripts add pcmanfm zenity neofetch adwaita-icon-theme menu-cache gvfs mesa-dri-gallium
fi

# Restore custom busybox and remove conflicting Alpine busybox symlinks for runit
cp -f "$BUSYBOX_DIR/busybox" "$ROOTFS_DIR/bin/busybox" 2>/dev/null || true
rm -f "$ROOTFS_DIR/usr/bin/runsv" "$ROOTFS_DIR/usr/bin/runsvdir" "$ROOTFS_DIR/usr/bin/runsvchdir" "$ROOTFS_DIR/usr/bin/sv" "$ROOTFS_DIR/usr/bin/svlogd" "$ROOTFS_DIR/usr/bin/chpst" "$ROOTFS_DIR/usr/bin/utmpset"

chmod +x "$ROOTFS_DIR/usr/bin/"* 2>/dev/null || true
chmod +x "$ROOTFS_DIR/bin/"* 2>/dev/null || true
chmod +x "$ROOTFS_DIR/etc/runit/3" 2>/dev/null || true

# Re-copy repository custom scripts to ensure they overwrite package defaults
rm -f "$ROOTFS_DIR/usr/bin/font" "$ROOTFS_DIR/usr/bin/rezzfont" "$ROOTFS_DIR/bin/font" "$ROOTFS_DIR/bin/rezzfont" 2>/dev/null || true
[ -d "$ROOTFS_DIR/etc" ] && chmod -R u+w "$ROOTFS_DIR/etc" 2>/dev/null || true
[ -d "$ROOTFS_DIR/usr" ] && chmod -R u+w "$ROOTFS_DIR/usr" 2>/dev/null || true
cp -a "$REPO_DIR/usr/"* "$ROOTFS_DIR/usr/" 2>/dev/null || true
cp -a "$REPO_DIR/etc/"* "$ROOTFS_DIR/etc/" 2>/dev/null || true
[ -d "$REPO_DIR/root" ] && cp -a "$REPO_DIR/root/"* "$ROOTFS_DIR/root/" 2>/dev/null || true

# Compile the setconsolefont helper.
#
# -static matters. Without it the host's gcc links against the host's glibc,
# so the binary asks the kernel for /lib64/ld-linux-x86-64.so.2 and libc.so.6.
# The rootfs is musl only, it ships /lib/ld-musl-x86_64.so.1 and no glibc, so
# the helper died with "not found" on every run. Both callers redirect that to
# /dev/null, which is why nobody noticed.
#
# Static linking also means the helper does not care which libc the build host
# has, which is the same reason swap-offset next to it is a static binary.
#
# Errors are no longer swallowed. If this stops compiling, that is worth
# knowing at build time rather than shipping a console font that never applies.
if [ -f "$REPO_DIR/usr/bin/setconsolefont.c" ]; then
    log "Compiling setconsolefont"
    gcc -O2 -static "$REPO_DIR/usr/bin/setconsolefont.c" \
        -o "$ROOTFS_DIR/usr/bin/setconsolefont"
    chmod +x "$ROOTFS_DIR/usr/bin/setconsolefont"
fi

step "Downloading rezz-utils"
REZZPAD_URL="https://github.com/neko-qt/rezzpad/releases/download/1.1.0/rezzpad-musl"
if [ ! -f "$CACHE_DIR/rezzpad-musl" ]; then
    wget -q "$REZZPAD_URL" -O "$CACHE_DIR/rezzpad-musl" || { echo "Failed to download rezzpad"; exit 1; }
fi
cp -f "$CACHE_DIR/rezzpad-musl" "$ROOTFS_DIR/usr/bin/rezzpad"
chmod +x "$ROOTFS_DIR/usr/bin/rezzpad"

REZZVIEW_URL="https://github.com/neko-qt/rezzview/releases/download/1.1.0/rezzview-musl"
if [ ! -f "$CACHE_DIR/rezzview-musl" ]; then
    wget -q "$REZZVIEW_URL" -O "$CACHE_DIR/rezzview-musl" || { echo "Failed to download rezzview"; exit 1; }
fi
cp -f "$CACHE_DIR/rezzview-musl" "$ROOTFS_DIR/usr/bin/rezzview"
chmod +x "$ROOTFS_DIR/usr/bin/rezzview"

REZZTOP_URL="https://github.com/neko-qt/rezztop/releases/download/1.1.0/rtop"
if [ ! -f "$CACHE_DIR/rtop" ]; then
    wget -q "$REZZTOP_URL" -O "$CACHE_DIR/rtop" || { echo "Failed to download rezztop"; exit 1; }
fi
cp -f "$CACHE_DIR/rtop" "$ROOTFS_DIR/usr/bin/rtop"
chmod +x "$ROOTFS_DIR/usr/bin/rtop"


chmod +x "$ROOTFS_DIR/usr/bin/"* 2>/dev/null || true
chmod +x "$ROOTFS_DIR/usr/share/udhcpc/default.script" 2>/dev/null || true
find "$ROOTFS_DIR/etc/runit/runsvdir" -type f -exec chmod +x {} + 2>/dev/null || true
find "$ROOTFS_DIR/etc/sv" -type f -exec chmod +x {} + 2>/dev/null || true

# Ensure editor executable symlinks exist
[ -f "$ROOTFS_DIR/usr/bin/nano" ] && ln -sf /usr/bin/nano "$ROOTFS_DIR/bin/nano" 2>/dev/null || true
[ -f "$ROOTFS_DIR/usr/bin/nano" ] && ln -sf /usr/bin/nano "$ROOTFS_DIR/bin/notepad" 2>/dev/null || true
if [ -f "$ROOTFS_DIR/usr/bin/neatvi" ]; then
    ln -sf /usr/bin/neatvi "$ROOTFS_DIR/bin/vi" 2>/dev/null || true
    ln -sf /usr/bin/neatvi "$ROOTFS_DIR/usr/bin/vi" 2>/dev/null || true
fi

# Make xinitrc executable and alias xterm to st
if [ -f "$ROOTFS_DIR/etc/X11/xinit/xinitrc" ]; then
    chmod +x "$ROOTFS_DIR/etc/X11/xinit/xinitrc" 2>/dev/null || true
    [ ! -f "$ROOTFS_DIR/root/.xinitrc" ] && cp -f "$ROOTFS_DIR/etc/X11/xinit/xinitrc" "$ROOTFS_DIR/root/.xinitrc" 2>/dev/null || true
fi
[ -f "$ROOTFS_DIR/root/.xinitrc" ] && chmod +x "$ROOTFS_DIR/root/.xinitrc" 2>/dev/null || true
[ -f "$ROOTFS_DIR/usr/bin/st" ] && ln -sf /usr/bin/st "$ROOTFS_DIR/usr/bin/xterm" 2>/dev/null || true

# Ensure font management tool symlinks exist
[ -f "$ROOTFS_DIR/usr/bin/font" ] && ln -sf /usr/bin/font "$ROOTFS_DIR/bin/font" 2>/dev/null || true
[ -f "$ROOTFS_DIR/usr/bin/font" ] && ln -sf /usr/bin/font "$ROOTFS_DIR/usr/bin/rezzfont" 2>/dev/null || true
[ -f "$ROOTFS_DIR/usr/bin/font" ] && ln -sf /usr/bin/font "$ROOTFS_DIR/bin/rezzfont" 2>/dev/null || true

# Ensure rezzconfig alias exists
[ -f "$ROOTFS_DIR/usr/bin/rezzconfig" ] && ln -sf /usr/bin/rezzconfig "$ROOTFS_DIR/usr/bin/config" 2>/dev/null || true
[ -f "$ROOTFS_DIR/usr/bin/rezzconfig" ] && ln -sf /usr/bin/rezzconfig "$ROOTFS_DIR/bin/config" 2>/dev/null || true

# Ensure rezzinstall and rezzkeymap aliases exist
[ -f "$ROOTFS_DIR/usr/bin/rezzinstall" ] && ln -sf /usr/bin/rezzinstall "$ROOTFS_DIR/usr/bin/install-rezzos" 2>/dev/null || true
[ -f "$ROOTFS_DIR/usr/bin/rezzinstall" ] && ln -sf /usr/bin/rezzinstall "$ROOTFS_DIR/sbin/rezzinstall" 2>/dev/null || true
[ -f "$ROOTFS_DIR/usr/bin/rezzkeymap" ] && ln -sf /usr/bin/rezzkeymap "$ROOTFS_DIR/usr/bin/keymap" 2>/dev/null || true

# Ensure sudo permissions and executable symlinks
if [ -f "$ROOTFS_DIR/usr/bin/sudo" ]; then
    chmod 4755 "$ROOTFS_DIR/usr/bin/sudo" 2>/dev/null || true
    ln -sf /usr/bin/sudo "$ROOTFS_DIR/bin/sudo" 2>/dev/null || true
    [ -f "$ROOTFS_DIR/etc/sudoers" ] && chmod 0440 "$ROOTFS_DIR/etc/sudoers" 2>/dev/null || true
fi

# Ensure power management command symlinks exist across sbin and bin
ln -sf /usr/bin/shutdown "$ROOTFS_DIR/sbin/shutdown" 2>/dev/null || true
ln -sf /usr/bin/poweroff "$ROOTFS_DIR/sbin/poweroff" 2>/dev/null || true
ln -sf /usr/bin/reboot "$ROOTFS_DIR/sbin/reboot" 2>/dev/null || true
ln -sf /usr/bin/hibernate "$ROOTFS_DIR/sbin/hibernate" 2>/dev/null || true
ln -sf /usr/bin/suspend "$ROOTFS_DIR/sbin/suspend" 2>/dev/null || true
ln -sf /usr/bin/poweroff "$ROOTFS_DIR/sbin/halt" 2>/dev/null || true

ln -sf /usr/bin/shutdown "$ROOTFS_DIR/bin/shutdown" 2>/dev/null || true
ln -sf /usr/bin/poweroff "$ROOTFS_DIR/bin/poweroff" 2>/dev/null || true
ln -sf /usr/bin/reboot "$ROOTFS_DIR/bin/reboot" 2>/dev/null || true
ln -sf /usr/bin/hibernate "$ROOTFS_DIR/bin/hibernate" 2>/dev/null || true
ln -sf /usr/bin/suspend "$ROOTFS_DIR/bin/suspend" 2>/dev/null || true
ln -sf /usr/bin/poweroff "$ROOTFS_DIR/bin/halt" 2>/dev/null || true



step "Packing rootfs.cpio.gz"
cd "$ROOTFS_DIR"
find . | cpio -o -H newc --owner 0:0 | gzip > "$REPO_DIR/rootfs.cpio.gz"
cd "$REPO_DIR"

# create disk.img
if [ ! -f "$REPO_DIR/disk.img" ]; then
    step "Creating disk.img"
    qemu-img create -f raw disk.img 1G
    mkfs.ext4 -F disk.img
fi

# Build the bootable ISO.
#
# The pieces have been in the repository for a while: iso/boot/grub/grub.cfg
# holds the menu, .gitignore already lists rezzos.iso, and the first step of
# the hardware install in the README is "download the ISO image". Nothing
# actually produced one, so that path started at a file that did not exist.
#
# Kept optional on purpose. grub-mkrescue and xorriso are not needed for a
# kernel plus rootfs build, so a missing toolchain is a note rather than a
# failed build, and the required-command list above stays as it was.
if command -v grub-mkrescue >/dev/null 2>&1 && command -v xorriso >/dev/null 2>&1; then
    step "Building rezzos.iso"
    ISO_DIR="$REPO_DIR/iso"
    mkdir -p "$ISO_DIR/boot/grub"

    if [ ! -f "$ISO_DIR/boot/grub/grub.cfg" ]; then
        echo "Missing $ISO_DIR/boot/grub/grub.cfg, cannot build the ISO."
        exit 1
    fi

    cp -f "$REPO_DIR/bzImage" "$ISO_DIR/boot/bzImage"
    cp -f "$REPO_DIR/rootfs.cpio.gz" "$ISO_DIR/boot/rootfs.cpio.gz"

    # grub-mkrescue is noisy on stderr about optional platforms it cannot find
    # (EFI images, mac boot). Those are warnings, so judge it by its exit code
    # and by whether an image came out.
    rm -f "$REPO_DIR/rezzos.iso"
    grub-mkrescue -o "$REPO_DIR/rezzos.iso" "$ISO_DIR" \
        -- -volid REZZOS

    if [ ! -s "$REPO_DIR/rezzos.iso" ]; then
        echo "grub-mkrescue reported success but produced no image."
        exit 1
    fi

    # The kernel and rootfs staged above are build output, not sources. Leave
    # the directory the way it was found so a rebuild does not accumulate.
    rm -f "$ISO_DIR/boot/bzImage" "$ISO_DIR/boot/rootfs.cpio.gz"
else
    log "Skipping rezzos.iso: install grub-mkrescue and xorriso to build it"
fi

step "Done"
ls -la bzImage rootfs.cpio.gz disk.img
# The script runs under `set -e` and this is the last command, so a plain
# `[ -f ... ] && ls` would exit 1 whenever the ISO was skipped.
if [ -f "$REPO_DIR/rezzos.iso" ]; then
    ls -la rezzos.iso
fi

if [ -f "$REPO_DIR/start.sh" ]; then
    echo "Run: ./start.sh"
else
    cat << 'EOF'

Run:
    qemu-system-x86_64 \
        -kernel bzImage \
        -initrd rootfs.cpio.gz \
        -append "console=ttyS0" \
        -netdev user,id=net0,hostfwd=tcp::2222-:22 -device virtio-net,netdev=net0 \
        -drive file=disk.img,format=raw,if=virtio \
        -m 512M -nographic
EOF
fi
