# Drivers in RezzOS

All drivers are built into the kernel (not modules).

**Network:**
- virtio-net (QEMU)
- e1000, e1000e (Intel)
- r8169 (Realtek)

**Disks:**
- virtio-blk (QEMU)
- SATA/AHCI

**USB:**
- XHCI (USB 3.0)
- EHCI (USB 2.0)
- USB-storage (flash drives)

**Video:**
- VESA framebuffer (basic)
- Intel i915
- AMDGPU
- Nouveau (NVIDIA)

**File systems:**
- ext4
- vfat (FAT32)
- iso9660 (CD/DVD)

## How to add a driver

1. Open `kernel.config` in the repository root
2. Add the line `CONFIG_DRIVER_NAME=y`
3. You can find the exact driver name in the kernel configuration: `make menuconfig`
4. Rebuild the kernel: `./build.sh`
