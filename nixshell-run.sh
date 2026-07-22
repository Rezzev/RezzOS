#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "Setup temp folder (path trouble)..."
rm -rf /tmp/RezzOS_build
cp -a . /tmp/RezzOS_build
cd /tmp/RezzOS_build

echo "Build RezzOS..."
nix-shell -E 'with import <nixpkgs> {}; gcc13Stdenv.mkDerivation { name = "env"; buildInputs = [ wget gnutar gnumake cpio gzip qemu e2fsprogs xz ncurses pkg-config flex bison bc elfutils openssl perl glibc.static ]; }' --run "./build.sh"

echo "Return collected images..."
cd - > /dev/null
cp /tmp/RezzOS_build/{bzImage,rootfs.cpio.gz,disk.img} .

echo "Start RezzOS GUI..."
nix-shell -p qemu --run "./start-gui.sh"