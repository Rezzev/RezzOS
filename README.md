<img width="250" height="250" alt="image" src="https://github.com/user-attachments/assets/92fc26a3-9e05-4cbf-8afc-f97e75501fa7" />








![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![BusyBox](https://img.shields.io/badge/BusyBox-000000?style=for-the-badge&logo=busybox&logoColor=white)
![Shell](https://img.shields.io/badge/Shell-4EAA25?style=for-the-badge&logo=gnu-bash&logoColor=white)

*more documentation in /docs*







## System
| Component | Version |
|-----------|---------|
| Linux | 6.6.40 LTS |
| BusyBox | 1.36.1 |                  
| Bash | 5.2 |
| musl | 1.2.5 |
| runit | 2.1.2 |

*These are just recommended versions, you can change them by editing build.sh!*

## Features
- Package manager (Alpine repos)
- Persistent ext4 storage
- Network with DHCP and DNS
- File downloader (ghget)

## Development
- TCC (Tiny C Compiler) with full musl-dev headers
- Lua 5.3 
- Write and compile C and Lua programs directly in RezzOS

## Build from Source
The easiest way is to run the included build script:
```bash
./build.sh
```
For NixOS:
```bash
./nixshell-run.sh
```
To build, you must have the dependencies installed.( dependencies in /docs/build dependencies.md)

It will automatically download sources, compile the kernel and BusyBox, assemble the rootfs, and create a disk image.

## Networking

**QEMU (virtual machine):**
```bash
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up
route add default gw 10.0.2.2
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```
**Real hardware:**
DHCP is used automatically. If network doesn't work:
```bash
ifconfig eth0 up
udhcpc -i eth0
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

## Usage
```bash
pkg install nano
pkg search python
ghget https://url file
sv status /etc/runit/runsvdir/default/*
sv restart tty1
```

## Quick Start
To get launched, use ./start.sh or start-gui.sh





## Links
- GitHub Repository - https://github.com/semen88pochuev-eng/RezzOS
- BusyBox - https://busybox.net/
- Linux kernel - https://kernel.org/
- Alpine Linux - https://alpinelinux.org/

## Contributors
Thanks to all contributors who help improve RezzOS!

- [@neko_qt](https://github.com/neko-qt) — improved build system, added runit support, fixed init script, auto-download sources
- [@Kenyka kenykovich](https://github.com/keeniGithub) - terminal history, fetch, added rezzmon system monitor, swap manager, pkg remove and shell profile
- [@wqreloxz](https://github.com/wqreloxz) - update init
- [@valtrynx](https://github.com/d1mazaurus7) - TUI Installer developer

## License
GNU General Public License v3.0
