# Émulateur AKA sous Windows

Deux binaires : `aka-front.exe` (SDL2, écran/boutons/son) et `qemu-system-xtensa.exe`
(ESP32-S3 + périphériques AKA). Le front-end compile et se lie avec mingw-w64 (vérifié en
cross-compilation) ; QEMU, qui dépend de glib/pixman, se compile dans **MSYS2**.

## Compiler
1. Installer MSYS2 (msys2.org), ouvrir **MSYS2 MINGW64**.
2. `bash windows/build-qemu-msys2.sh` (≈ 20-40 min) puis `bash windows/build-front-msys2.sh`.
3. Résultat dans `windows/dist/` : `aka-front.exe`, `SDL2.dll`, `qemu/`.

## Lancer
Copier `partitions.bin`, `bootloader.bin` (+ `launcher.bin`) dans `dist/`, puis :

    aka-front.exe --qemu qemu\qemu-system-xtensa.exe --bios qemu\pc-bios ^
        --partitions partitions.bin --bootloader bootloader.bin --launcher launcher.bin ^
        --flash aka-flash.bin --sd C:\chemin\macarte

(ou mettre ces options dans `aka-emu.cfg`, voir `front/README.md`).

## Différences avec Linux
- Les fichiers partagés QEMU↔front-end sont dans `%TEMP%\aka-emu\` (fb, input, audio) au lieu de `/dev/shm`.
- Plus besoin de variable d'environnement pour la SD en dossier (`fat:rw:`).
- Les fichiers partagés ne sont pas supprimés au redémarrage de QEMU (Windows l'interdit tant qu'ils sont mappés) : QEMU les réinitialise.
- Non testé sur une vraie machine Windows (pas disponible dans mon environnement) : signale-moi tout souci.
