<!--
  README.md — RezzOS
  Improved, polished, and translated English version
-->

# RezzOS

A minimalist Linux distribution built around the Linux 6.6.40 kernel and BusyBox, designed for simplicity, portability, and a lightweight system footprint.

## About the project

RezzOS is a small and efficient Linux-based operating system aimed at users who value minimalism, control, and a compact build. It is built using a simple yet reliable toolchain and a lightweight userspace based on BusyBox.

The project is intended to be easy to understand, build, and customize, while still providing a usable system for everyday tasks in both desktop and server-oriented setups.

### Core components

| Component | Recommended version |
|-----------|---------------------:|
| Linux     | 6.6.40 (LTS)        |
| BusyBox   | 1.36.1              |
| Bash      | 5.2                 |
| musl      | 1.2.5               |
| runit     | 2.1.2               |

> If you need to build with different versions, update the values in `build.sh`.

---

## Documentation

- [PROGRAMS](/docs/COMMANDS.md) — unique programs and commands
- [BUILD](/docs/BUILD_GUIDE.md) — build guide from source
- [CONTRIBUTING](/docs/CONTRIBUTING.md) — information for contributors
- [HOW INSTALL](/docs/HOW_INSTALL.md) — installation guide
- [NUANCES](/docs/NUANCES.md) — usage notes and subtle details

---


### Package manager

RezzOS supports both Alpine repositories and a custom package repository.

```bash
apk update    # from the Alpine repository
pkg update    # from the RezzOS repository
```

This setup provides a lightweight package management approach with compatibility and flexibility.

### Minimalistic design

The system is built with a small footprint in mind, making it suitable for resource-constrained environments, custom builds, and users who prefer a clean and stripped-down Linux experience.

### Custom system tooling

The project includes various custom utilities, scripts, and configuration layers to support service management, system setup, package integration, and operational convenience.

---

## Quick start (in QEMU)

From the repository root, run:

```bash
./start-gui.sh
```

This launches the system inside QEMU with a graphical interface, assuming it has been built beforehand.

---

## Links

- Main repository: [open](https://github.com/Rezzev/RezzOS)
- Package repository: [open](https://github.com/Rezzev/RezzOS-Packages)

---

## Contributors

Thanks to everyone who contributes to the development of this project.

- [@Rezzev](https://github.com/Rezzev) — project creator, architecture, build system, Lua integration, support, and general development
- [@Kenyka kenykovich](https://github.com/keeniGithub) — GUI, JWM integration, desktop environment work, SSH, multi-user support, service management, interface improvements
- [@neko_qt](https://github.com/neko-qt) — rezz utils, build improvements, kernel configuration, init fixes
- [@drawiks](https://github.com/drawiks) — Wi-Fi stack, rezzwifi, iwd, rezzusb, rezztz, installation fixes, kernel configuration, and other improvements
- [@tanukis0408](https://github.com/tanukis0408) — package updates, fixes, and other contributions
- [@wqreloxz](https://github.com/wqreloxz) — service scripts, package manager, init improvements
- [@TOPDATOP](https://github.com/topdatop01) — wireless support, iwd, dhcpcd
- [@thebiggestlarp](https://github.com/thebiggestlarp) — developer of rezzfetch
- [@superuserdo-sudo](https://github.com/superuserdo-sudo) — init security layer development

### Contact

- Telegram: @Loexez
- Telegram channel: @Losk149

---

## License

This project is distributed under the GNU General Public License v3.0.
