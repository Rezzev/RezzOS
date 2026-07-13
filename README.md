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

```
## Quick Start

```bash
qemu-system-x86_64 \
    -kernel bzImage \
    -initrd rootfs.cpio.gz \
    -append "console=ttyS0 quiet loglevel=0 root=/dev/ram0 init=/init" \
    -netdev user,id=net0 -device virtio-net,netdev=net0 \
    -drive file=disk.img,format=raw,if=virtio \
    -m 512M -nographic
```
## Links
- GitHub Repository - https://github.com/semen88pochuev-eng/RezzOS
- BusyBox - https://busybox.net/
- Linux kernel - https://kernel.org/
- Alpine Linux - https://alpinelinux.org/


## License
GNU General Public License v3.0
