<!--
  README.md — RezzOS
  Improved and formatted version of the README in English
-->

# RezzOS - Linux distro 

**A minimalist Linux build based on the Linux 6.6.40 kernel and BusyBox**

## About the system


| Component | Recommended version |
|-----------|---------------------:|
| Linux     | 6.6.40 (LTS)        |
| BusyBox   | 1.36.1              |
| Bash      | 5.2                 |
| musl      | 1.2.5               |
| runit     | 2.1.2               |

> Change versions in <code>build.sh</code> if you need to build different versions.

## Documentation 
- [PROGRAMS](/docs/COMMANDS.md)  - уникальные программы и команды 
- [BUILD](/docs/BUILD_GUIDE.md) - Гайд на сборку из исходников
- [CONTRIBUTING](/docs/CONTRIBUTING.md) - информация для контербьюетров 
- [HOW INSTALL](/docs/HOW_INSTALL.md) - гайд на установку
- [NUANCES](/docs/NUANCES.md) - нюансы при использовании

## Features
### Package manager (Alpine repositories and its own repository)
```bash
apk update - from alpine repository
pkg update - from our repository
```

## Quick start (in QEMU)
Run from the repository root:

```bash
./start-gui.sh    # with GUI (if built)
```

---

## Links
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
