# Инструкция по сборке RezzOS
Следуйте данной инструкции для сборки ос на вашем ПК
## Установка необходимого 
Скачиваем репозиторий:
```bash
git clone https://github.com/Rezzev/RezzOS.git
```
Установка зависимостей для сборки
```bash
apk add build-base flex bison linux-headers elfutils-dev openssl-dev perl bc cpio gzip wget tar grub-bios xorriso qemu-system-x86_64 qemu-img fakeroot gtk+3.0-dev gtksourceview-dev webkit2gtk-4.1 libxcomposite-dev libxrender-dev libxfixes-dev libxdamage-dev libx11-dev e2fsprogs gtk+3.0-dev webkit2gtk-4.1-dev
```
!!ВАЖНО!!

*Вам нужен установленный alpine linux в docker или полноценный alpine на вашем ПК*

*Иначее собсветнные программы не смогут собратся под musl*

*Вы моежете попробывать собрать на другом musl дистрибутиве,но рекомендуется alpine*

## Сборка
Запускай сборку из под root или с помошью sudo/doas
```bash
sudo/doas ./build.sh
```
Начнется процесс сборки ОС:
```
Busybox -> Kernel -> Build Programs -> Create rootfs -> Create ISO
```
после успешной сборки у вас будет готовый ISO-образ
