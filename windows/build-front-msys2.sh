#!/usr/bin/env bash
# Compile aka-front.exe (SDL2) dans un terminal MSYS2 MINGW64.
set -e
cd "$(dirname "$0")/../front"
g++ -O2 -std=c++17 main.cpp flashimg.cpp -o ../windows/dist/aka-front.exe \
    $(sdl2-config --cflags --libs) -static-libgcc -static-libstdc++
cp "$(dirname "$(which sdl2-config)")/SDL2.dll" ../windows/dist/ 2>/dev/null || true
echo "OK -> windows/dist/aka-front.exe"
