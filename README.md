# RezzOS - open source linux distubition 
<img width="351" height="342" alt="image" src="https://github.com/user-attachments/assets/53dd2f1d-352f-475c-938c-9bf36bea8aea" />

## System
| Component | Version |
|-----------|---------|
| Linux | 6.6.40 LTS |
| BusyBox | 1.36.1 |
| GNU Bash | 5.2 |
| musl | 1.2.5 |
| nano | 8.0 |
| Xorg | 21.1 |


## Features
- Package manager (Alpine repos)
- Persistent ext4 storage
- Network with DHCP and DNS
- File downloader (ghget)


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

- [@neko-qt](https://github.com/neko-qt) — improved build system, added runit support, fixed init script, auto-download sources

## License
GNU General Public License v3.0
