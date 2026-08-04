<!--
  README.md — RezzOS
  Улучшённая и оформленная версия README на русском
-->

<div align="center">

<img src="docs/assets/baner.jpg" alt="RezzOS Banner" width="100%" />

# <img width="40" height="40" src="https://github.com/user-attachments/assets/2ea53faa-fcd1-4380-b317-6dc3e521ccd4" alt="RezzOS logo" /> RezzOS

**Минималистичная Linux-сборка на ядре Linux 6.6.40 и BusyBox**


[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://kernel.org)
[![BusyBox](https://img.shields.io/badge/BusyBox-000000?style=for-the-badge&logo=busybox&logoColor=white)](https://busybox.net)
[![Shell](https://img.shields.io/badge/Shell-4EAA25?style=for-the-badge&logo=gnu-bash&logoColor=white)](https://www.gnu.org/software/bash/)

<img src="https://github.com/user-attachments/assets/cccdda8c-9d78-4aa2-9ef5-7dc5674d9324" alt="screenshot" width="90%" />

<p><em>Дополнительная документация — в каталоге <code>/docs</code>.</em></p>

</div>

---

## Содержание
- [О системе](#о-системе)
- [Особенности](#особенности)
- [RezzUtils](#rezzutils)
- [Разработка](#разработка)
- [Сборка из исходников](#сборка-из-исходников)
- [Сеть](#сеть)
- [Быстрый старт (QEMU)](#быстрый-старт-qemu)
- [Ссылки](#ссылки)
- [Контакты и вкладчики](#контакты-и-вкладчики)
- [Лицензия](#лицензия)

---

## О системе
Текущая рекомендованная конфигурация:

| Компонент | Рекомендуемая версия |
|-----------|---------------------:|
| Linux     | 6.6.40 (LTS)        |
| BusyBox   | 1.36.1              |
| Bash      | 5.2                 |
| musl      | 1.2.5               |
| runit     | 2.1.2               |

> Это — рекомендации. Изменяйте версии в <code>build.sh</code>, если требуется другая сборка.

## Особенности
- Менеджер пакетов (репозитории Alpine)
- Постоянное хранилище (ext4)
- Сеть с DHCP и DNS
- Лёгкая и быстрая среда разработки: TCC и Lua в корне системы

## RezzUtils
Встроенный набор утилит для удобной работы:

- rezzpad — простой текстовый блокнот
- rezztop — монитор системных ресурсов
- rezzview — просмотрщик изображений

Исходники и список пакета: [Rezz-utils source](https://github.com/stars/neko-qt/lists/rezz-utils)

## Разработка
RezzOS предоставляет минимальную среду для разработки прямо внутри образа:
- TCC (Tiny C Compiler) с заголовками musl
- Lua 5.3 — быстро писать и запускать скрипты
- Компиляция и запуск C / Lua программ прямо в системе

---

## Сборка из исходников
Самый простой способ — использовать встроенный скрипт сборки:

```bash
./build.sh
```

Для NixOS используйте:

```bash
./nixshell-run.sh
```

Перед сборкой установите зависимости (список в <code>/docs/build dependencies.md</code>).
Скрипт автоматически скачает исходники, скомпилирует ядро и BusyBox, соберёт rootfs и создаст образ диска.

---

## Сеть
Если вы используете QEMU (виртуальная машина), можно настроить сеть вручную:

```bash
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up
route add default gw 10.0.2.2
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

На реальном железе используется DHCP. Если сеть не поднимается:

```bash
ifconfig eth0 up
udhcpc -i eth0
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

Перед использованием менеджера пакетов выполните:

```bash
pkg update
```

---

## Быстрый старт (в QEMU)
Запуск из корня репозитория:

```bash
./start.sh        # текстовый режим
./start-gui.sh    # с GUI (если собран)
```

---

## Полезные ссылки
- Репозиторий: https://github.com/semen88pochuev-eng/RezzOS
- BusyBox: https://busybox.net/
- Linux kernel: https://kernel.org/
- Alpine Linux: https://alpinelinux.org/

---

## Контакты и вкладчики
Спасибо всем, кто помогает развивать проект!

- [@semen88pochuev-eng](https://github.com/semen88pochuev-eng) (rezzev) — создатель проекта, архитектура, система сборки, интеграция Lua, поддержка образов и GRUB.
- [@Kenyka kenykovich](https://github.com/keeniGithub) — GUI (JWM), интеграция рабочего стола, SSH, multi-user, управление службами, интерфейсы конфигурации.
- [@neko_qt](https://github.com/neko-qt) — Rezz utils, улучшения сборки, конфигурация ядра, исправления init.
- [@wqreloxz](https://github.com/wqreloxz) — скрипты сервисов, менеджер пакетов, улучшения init.
- [@TOPDATOP](https://github.com/topdatop01) — поддержка беспроводных сетей (iwd, dhcpcd).

Контакты автора:
- Telegram: @Loexez

---

## Лицензия
Проект распространяется под GNU General Public License v3.0
