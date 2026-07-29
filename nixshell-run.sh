#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "Build RezzOS..."
nix-shell -E 'with import <nixpkgs> {}; gcc13Stdenv.mkDerivation { name = "env"; buildInputs = [ wget gnutar gnumake cpio gzip qemu e2fsprogs xz ncurses pkg-config flex bison bc elfutils openssl perl glibc.static fakeroot ]; }' --run "./build.sh"

echo "Start RezzOS GUI..."
nix-shell -p qemu --run "./start-gui.sh"