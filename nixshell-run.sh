#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "Setup temp folder for build..."
mkdir -p /tmp/RezzOS_build
# Copy project files while excluding heavy source trees
find . -mindepth 1 -maxdepth 1 ! -name 'linux-6.6.40' ! -name 'busybox-1.36.1' ! -name '.git' -exec cp -a {} /tmp/RezzOS_build/ \;
[ -f bzImage ] && cp bzImage /tmp/RezzOS_build/ 2>/dev/null || true
cd /tmp/RezzOS_build

echo "Build RezzOS..."
nix-shell -E 'with import <nixpkgs> {}; gcc13Stdenv.mkDerivation { name = "env"; buildInputs = [ wget gnutar gnumake cpio gzip qemu e2fsprogs xz ncurses pkg-config flex bison bc elfutils openssl perl glibc.static ]; }' --run "./build.sh"

echo "Return collected images..."
cd - > /dev/null
cp /tmp/RezzOS_build/{bzImage,rootfs.cpio.gz,disk.img} . 2>/dev/null || true

echo "Start RezzOS GUI..."
nix-shell -p qemu --run "./start-gui.sh"