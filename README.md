# Émulateur Gamebuino AKA (PC)

Fait tourner le **vrai firmware ESP32-S3** (.bin) dans QEMU, avec un front-end SDL2 :
écran 320×240, boutons/joystick, son, carte SD simulée par un dossier, flash construite
depuis `partitions.bin` (Launcher dans app1, jeux installés dans app0, comme sur la console).

## Démarrage rapide (Linux)
    sudo apt install libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 libfdt1
    ./run.sh                      # utilise aka-emu.cfg (Launcher + SD d'exemple avec pAKAman)

Dans le Launcher : **A** installe/lance le jeu choisi, **D** relit la SD.
Retour au Launcher : **HOME+MENU** (500 ms). **L1+R1** relance le dernier jeu.
Touches PC : flèches, IJKL = joystick (voir boutons à l'écran) ; **F4** quitter, **F5** relancer QEMU,
**F6** refaire la flash (efface les jeux installés), **F7** muet.

## Contenu
- `bin/` : `aka-front` (front-end), `qemu-system-xtensa` (build PGO, ESP32-S3 + périphériques AKA), `pc-bios/` (ROM ESP32-S3)
- `sample/` : `real/` (partitions.bin, bootloader.bin, launcher.bin) et `sd/` (exemple : PAKAMAN)
- `front/` : sources du front-end (`main.cpp`, `flashimg.*`, `platform.h`) + options détaillées (README)
- `qemu-patch/` : patch `aka-qemu.patch` à appliquer sur le fork Espressif de QEMU (commit indiqué dans `qemu-patch/README.md`)
- `windows/` : scripts MSYS2 pour compiler QEMU et le front-end sous Windows, `dist-cross/` (front-end .exe + SDL2.dll), `ANDROID.md`
- `tools/` : `aka_flash.py`, `aka_sd.py` ; `aka-launcher.zip` : sources du Launcher ; PDF : mode d'emploi du Launcher

## Utiliser tes propres fichiers
Remplace `sample/real/*` par ta table de partitions, ton bootloader et ton Launcher (les offsets viennent
de `partitions.bin`, rien n'est codé en dur). Une SD = un dossier par appli (`firmware.bin`, `meta.json`, `screen.bpm`).
Si le Launcher est flashé en app0, il se recopie lui-même en app1 au premier démarrage.

## Périphériques émulés
Écran ST7789 via LCD_CAM+GDMA, TE 60 Hz, boutons (expander I2C), joystick+batterie (ADC calibré),
SD (SPI), flash SPI + cache avec OTA (esp_ota_*), SHA, audio I2S → SDL (44,1 kHz mono).

## Recompiler QEMU
Prendre le fork Espressif QEMU (commit dans `qemu-patch/README.md`), `git apply qemu-patch/aka-qemu.patch`, puis :
`../configure --target-list=xtensa-softmmu --disable-docs --disable-tools --disable-user --disable-gtk --disable-vnc --disable-sdl --enable-fdt=system --disable-debug-info && ninja qemu-system-xtensa`
(Le patch référence déjà `aka_i2s.c` dans `hw/misc/meson.build`.)

## Limites connues
Non testé sur Windows/Android réels ; ADC joystick saturé près de 3,2 V ; le jeu sature à ~60 i/s (plafond du jeu).

## Releases GitHub
Pousser un tag `vX.Y` déclenche `.github/workflows/release.yml` : build Linux et Windows (MSYS2), puis publication
des archives `aka-emulator-linux-x64.tar.gz` et `aka-emulator-windows-x64.zip` dans la release.
`ci.yml` vérifie à chaque push que le front-end compile.
