<!--
  README.md — RezzOS
  Improved and formatted version of the README in English
-->

# <img width="40" height="40" src="https://github.com/user-attachments/assets/2ea53faa-fcd1-4380-b317-6dc3e521ccd4" alt="RezzOS logo" /> RezzOS - simple distribution

**A minimalist Linux build based on the Linux 6.6.40 kernel and BusyBox**

[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://kernel.org)
[![BusyBox](https://img.shields.io/badge/BusyBox-000000?style=for-the-badge&logo=busybox&logoColor=white)](https://busybox.net)
[![Shell](https://img.shields.io/badge/Shell-4EAA25?style=for-the-badge&logo=gnu-bash&logoColor=white)](https://www.gnu.org/software/bash/)


## About the system


| Component | Recommended version |
|-----------|---------------------:|
| Linux     | 6.6.40 (LTS)        |
| BusyBox   | 1.36.1              |
| Bash      | 5.2                 |
| musl      | 1.2.5               |
| runit     | 2.1.2               |

> Change versions in <code>build.sh</code> if you need to build different versions.

## Features
### Package manager (Alpine repositories and its own repository)
```bash
apk update - from alpine repository
pkg update - from our repository
```
### Key system utilities
- Rcomp - composer(x11)
- Redit - code editor
- Rbrowser - simple built-in browser on webkit2gtk
- Rconfig - settings menu
- Rinstall - minimal graphical installer
*These aren't all the utilities.*
### Other
- Persistent storage (ext4)
- Lightweight and fast development environment: TCC and Lua in the root filesystem



## Installation on hardware
```bash
startx # Start GUI
```
Open installer
Fill in the input fields and install the system.

## Building from source
If you are NOT using Alpine Linux:
Install alpine in Docker

Then, install the build dependencies:
```bash
apk add build-base flex bison linux-headers elfutils-dev openssl-dev perl bc cpio gzip wget tar grub-bios xorriso qemu-system-x86_64 qemu-img fakeroot gtk+3.0 gtksourceview webkit2gtk libxcomposite-dev libxrender-dev libxfixes-dev libxdamage-dev libx11-dev e2fsprogs gtk+3.0-dev gtksourceview-dev 
```

---



## Before using the package manager run:

```bash
apk update - download from alpine linux repository 
pkg update - download from RezzOS linux repository 
```

---

## Quick start (in QEMU)
Run from the repository root:

```bash
./start.sh        # text mode
./start-gui.sh    # with GUI (if built)
```

---

## links
- Main repository: [open](https://github.com/Rezzev/RezzOS)
- Pkg repository: [open](https://github.com/Rezzev/RezzOS-Packages)

---

## Contacts and contributors
Thanks to everyone who helps develop the project!

- [@Rezzev](https://github.com/Rezzev) — project creator, architecture, build system, Lua integration, support [...]
- [@Kenyka kenykovich](https://github.com/keeniGithub) — GUI (JWM), desktop integration, SSH, multi-user, service management, interface work [...]
- [@neko_qt](https://github.com/neko-qt) — rezz utils, build improvements, kernel configuration, init fixes.
- [@drawiks](https://github.com/drawiks) — wifi stack (rezzwifi, iwd), rezzusb, rezztz, fix rezzinstall, kernel config and other fixes.
- [@tanukis0408](https://github.com/tanukis0408) - updated and fixed pkg, other fixes
- [@wqreloxz](https://github.com/wqreloxz) — service scripts, package manager, init improvements.
- [@TOPDATOP](https://github.com/topdatop01) — wireless support (iwd, dhcpcd).
- [@thebiggestlarp](https://github.com/thebiggestlarp) - developer rezzfetch
- [@superuserdo-sudo](https://github.com/superuserdo-sudo) - init security layer devloper

Author contact:
- Telegram: @Loexez
Tg channel:
- @Losk149

---

## License
This project is distributed under the GNU General Public License v3.0
