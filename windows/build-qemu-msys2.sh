#!/usr/bin/env bash
# Compile qemu-system-xtensa.exe (fork Espressif + peripheriques AKA) sous Windows.
# A lancer dans un terminal "MSYS2 MINGW64" (https://www.msys2.org), depuis la racine du projet :
#     bash windows/build-qemu-msys2.sh
set -e
pacman -S --needed --noconfirm \
  mingw-w64-x86_64-toolchain mingw-w64-x86_64-glib2 mingw-w64-x86_64-pixman \
  mingw-w64-x86_64-libslirp mingw-w64-x86_64-SDL2 \
  base-devel git ninja python mingw-w64-x86_64-python-setuptools mingw-w64-x86_64-meson

cd "$(dirname "$0")/../qemu"
mkdir -p build-win && cd build-win
../configure --target-list=xtensa-softmmu --disable-docs --disable-tools --disable-user \
  --disable-gtk --disable-vnc --disable-sdl --disable-werror --disable-guest-agent \
  --enable-slirp --disable-debug-info --extra-cflags="-O3"
ninja qemu-system-xtensa.exe

# Dossier distribuable : qemu + DLL + bios + front-end
OUT=../../windows/dist
mkdir -p "$OUT/qemu"
cp qemu-system-xtensa.exe "$OUT/qemu/"
cp -r ../pc-bios "$OUT/qemu/pc-bios"
# DLL necessaires (ldd + filtre MSYS2)
ldd qemu-system-xtensa.exe | awk '/mingw64/ {print $3}' | xargs -I{} cp {} "$OUT/qemu/"
echo "OK -> $OUT/qemu/qemu-system-xtensa.exe"
