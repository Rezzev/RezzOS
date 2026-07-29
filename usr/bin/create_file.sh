#!/bin/bash
mkdir -p "$HOME/Desktop"
FILENAME=$(zenity --entry --title="Создать файл" --text="Введите имя файла:" --width=300)
if [ -n "$FILENAME" ]; then
    touch "$HOME/Desktop/$FILENAME"
fi
