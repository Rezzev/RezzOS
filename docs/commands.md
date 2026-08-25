## Power Management
| Command | Description |
|---------|----------|
| `poweroff` | Shut down system cleanly |
| `reboot` | Reboot system cleanly |
| `hibernate` | Suspend system state to disk/RAM |
| `shutdown [opts] [time]` | Multi-purpose system shutdown utility (`-h`, `-r`, `-z`, `now`) |

## Package Manager
| Command | Description |
|---------|----------|
| `pkg update` | Refresh package indexes |
| `pkg install <name>` | Install package |
| `pkg search <name>` | Search for packages |
| `pkg list` | List installed packages |
| `pkg upgrade` | Upgrade all packages |
| `pkg remove <name>` | Remove package |
| `pkg info <name>` | Package information |

## Services (runit)
| Command | Description |
|---------|----------|
| `rsv list` | List enabled services & their status |
| `rsv available` | List all available services in `/etc/sv` |
| `rsv status <name>` | Show status of a specific service |
| `rsv log <name>` | View log file of a specific service |
| `rsv up <name>` | Start a service |
| `rsv down <name>` | Stop a service |
| `rsv restart <name>` | Restart a service |
| `rsv enable <name>` | Enable service (symlink to auto-start at boot) |
| `rsv disable <name>` | Disable service (remove auto-start symlink) |
| `rsv create <name> "<cmd>"` | Create a new custom service in `/etc/sv` |

## System & Setup
| Command | Description                                                                                   |
|---------|-----------------------------------------------------------------------------------------------|
| `rezzinstall` / `install-rezzos` | Interactive TUI installer to install RezzOS onto disk/SSD with initial setup                  |
| `rezzconfig` / `config` | Interactive TUI control center & system configuration                                         |
| `rezzkeymap [set\|ru\|us]` | Keyboard & language layout switcher (RU/EN, toggle hotkeys)                                   |
| `rezzuser` | User management utility (add, remove, passwd, lock, groups)                                   |
| `rezzfetch` | System info banner & specs                                                                    |
| `rezzhw` | Hardware diagnostics utility                                                                  |
| `rezzdoctor` | System diagnostic & health check                                                              |
| `rezzwifi` | Wi-Fi manager: status, scan, connect, saved networks                                          |
| `rezzusb` | USB/removable disk manager: list, mount, umount, eject                                 |
| `rezztz` | Timezone viewer/setter: list zones in a region, set & persist a timezone                        |
| `rezzrecovery` | RezzOS recovery utility. Allows you to check the system, enter the recovery shell, and reboot. |
| `swap create [size]` | Create & activate swap file on persistent disk                                                |
| `swap status` | Check swap memory status                                                                      |

## Font & Display
| Command | Description |
|---------|----------|
| `font list` | List all available built-in kernel console fonts |
| `font set <name>` | Change OS font (e.g. `font set SUN12x22` or `font set TER16x32`) |
| `font current` | Show currently active OS font |

## Development
| Command | Description |
|---------|----------|
| `tcc <file.c>` | Compile C code |
| `lua5.3 <file.lua>` | Run Lua script |
| `make` | Build projects |
