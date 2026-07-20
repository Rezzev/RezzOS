$$
\Huge\color{white}{\text{Rezz}}\ \color{blue}{\text{OS}}
$$








![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![BusyBox](https://img.shields.io/badge/BusyBox-000000?style=for-the-badge&logo=busybox&logoColor=white)
![Shell](https://img.shields.io/badge/Shell-4EAA25?style=for-the-badge&logo=gnu-bash&logoColor=white)


<img width="250" height="125" alt="image" src="https://github.com/user-attachments/assets/b71f0ff1-4181-48f7-a334-13ffe1439ee0" />








## System
| Component | Version |
|-----------|---------|
| Linux | 6.6.40 LTS |
| BusyBox | 1.36.1 |                  
| GNU Bash | 5.2 |
| musl | 1.2.5 |
| nano | 8.0 |

*These are just recommended versions, you can change them by editing build.sh!*

## Features
- Package manager (Alpine repos)
- Persistent ext4 storage
- Network with DHCP and DNS
- File downloader (ghget)

## Development
- TCC (Tiny C Compiler) with full musl-dev headers
- Write and compile C programs directly in RezzOS

## Usage
```bash
pkg install nano
pkg search python
ghget https://url file
sv status /etc/runit/runsvdir/default/*
sv restart tty1
```
## Manual build
See build.sh for detailed steps.

## Quick Start
To get launched, use ./start.sh or start-gui.sh

## Build from Source
The easiest way is to run the included build script:
```bash
./build.sh
```
It will automatically download sources, compile the kernel and BusyBox, assemble the rootfs, and create a disk image.



## Links
- GitHub Repository - https://github.com/semen88pochuev-eng/RezzOS
- BusyBox - https://busybox.net/
- Linux kernel - https://kernel.org/
- Alpine Linux - https://alpinelinux.org/

## Contributors
Thanks to all contributors who help improve RezzOS!

- [@neko_qt](https://github.com/neko-qt) — improved build system, added runit support, fixed init script, auto-download sources
- [@wqreloxz](https://github.com/wqreloxz) - update init
- [@valtrynx](https://github.com/d1mazaurus7) - TUI Installer developer

## License
GNU General Public License v3.0
