<!--
  README.md — RezzOS
  Improved and formatted version of the README in English
-->
<img src="docs/assets/baner.jpg" alt="RezzOS Banner" width="100%" />

# <img width="40" height="40" src="https://github.com/user-attachments/assets/2ea53faa-fcd1-4380-b317-6dc3e521ccd4" alt="RezzOS logo" /> RezzOS

**A minimalist Linux build based on the Linux 6.6.40 kernel and BusyBox**

[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://kernel.org)
[![BusyBox](https://img.shields.io/badge/BusyBox-000000?style=for-the-badge&logo=busybox&logoColor=white)](https://busybox.net)
[![Shell](https://img.shields.io/badge/Shell-4EAA25?style=for-the-badge&logo=gnu-bash&logoColor=white)](https://www.gnu.org/software/bash/)

<img width="640" height="320" alt="image" src="https://github.com/user-attachments/assets/49f8b972-04a9-4133-acf9-8a16d2d92cbd" />

<p><em>Additional documentation is in the <code>/docs</code> directory.</em></p>



---

## Contents
- [About the system](#about-the-system)
- [How install](#Installation-on-hardware)
- [Features](#features)
- [RezzUtils](#rezzutils)
- [Development](#development)
- [Building from source](#building-from-source)
- [Network](#network)
- [Quick start (QEMU)](#quick-start-qemu)
- [Links](#links)
- [Contacts and contributors](#contacts-and-contributors)
- [License](#license)

---

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
- Package manager (Alpine repositories)
- Persistent storage (ext4)
- Network with DHCP and DNS
- Built-in diagnostics: `rezzdoctor` checks storage, services, network and memory, and reports what is wrong
- Lightweight and fast development environment: TCC and Lua in the root filesystem

## RezzUtils
Built-in utilities for convenient work:

- rezzpad — a simple text notepad
- rezztop — a system resource monitor
- rezzview — an image viewer

Sources and package list: [Rezz-utils source](https://github.com/stars/neko-qt/lists/rezz-utils)

## Installation on hardware
For execution in RAM(default):
- Download the ISO image.
- Save it to a flash drive.
- And boot from it.

For loading from disk:
- Follow the steps outlined above.
- After starting the system, enter these command:
```bash
rezzinstall
```

## Building from source
The simplest way is to use the included build script:

```bash
./build.sh
```

For NixOS use:

```bash
./nixshell-run.sh
```

Before building, install dependencies (list in <code>/docs/build dependencies.md</code>).
The script will automatically download sources, compile the kernel and BusyBox, assemble the rootfs, and create a disk image.

---

## Network
If you are using QEMU (virtual machine), you can configure the network manually:

```bash
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up
route add default gw 10.0.2.2
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

On real hardware DHCP is used. If the network does not come up:

```bash
dhcpcd eth0
```

Before using the package manager run:

```bash
pkg update
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
- Main repository: [open](https://github.com/semen88pochuev-eng/RezzOS)
- Pkg repository: [open](https://github.com/Rezzev/RezzOS-Packages)

---

## Contacts and contributors
Thanks to everyone who helps develop the project!

- [@Rezzev](https://github.com/Rezzev) — project creator, architecture, build system, Lua integration, support [...]
- [@Kenyka kenykovich](https://github.com/keeniGithub) — GUI (JWM), desktop integration, SSH, multi-user, service management, interface work [...]
- [@neko_qt](https://github.com/neko-qt) — rezz utils, build improvements, kernel configuration, init fixes.
- [@tanukis0408](https://github.com/tanukis0408) - updated and fixed pkg, other fixes
- [@wqreloxz](https://github.com/wqreloxz) — service scripts, package manager, init improvements.
- [@TOPDATOP](https://github.com/topdatop01) — wireless support (iwd, dhcpcd).
- [@nevvixsz](https://github.com/nevvixsz) - devloper installer
- [@thebiggestlarp](https://github.com/thebiggestlarp) - developer rezzfetch

Author contact:
- Telegram: @Loexez

---

## License
This project is distributed under the GNU General Public License v3.0
