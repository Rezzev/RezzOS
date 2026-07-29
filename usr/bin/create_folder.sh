#!/bin/bash
mkdir -p "$HOME/Desktop"
DIRNAME=$(zenity --entry --title="Создать папку" --text="Введите имя папки:" --width=300)
if [ -n "$DIRNAME" ]; then
    mkdir -p "$HOME/Desktop/$DIRNAME"
fi
