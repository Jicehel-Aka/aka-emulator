# Émulateur Gamebuino AKA — front-end SDL2

`aka-front` pilote tout : il fabrique la flash depuis la table de partitions (comme l'AKA),
lance QEMU (ESP32-S3), affiche l'écran + boutons/joystick et publie les entrées.

Compilation : `g++ -O2 -std=c++17 main.cpp flashimg.cpp -o aka-front $(sdl2-config --cflags --libs)`

Exemple :

    ./aka-front --qemu ./qemu-system-xtensa --bios pc-bios \
        --partitions partitions.bin --bootloader bootloader.bin --launcher launcher.bin \
        --flash aka-flash.bin --sd ./macarte

- La flash est créée au premier lancement, puis **conservée** (les jeux installés restent). `--fresh` ou **F6** la refait.
- `--sd DOSSIER` : un dossier sert de carte SD (un sous-dossier par application : firmware.bin, meta.json, screen.bpm).
  `--sd-img carte.img` : image FAT32 existante.
- `--launcher-slot 0|1` (défaut 1 = app1) ; `--game firmware.bin` pré-installe un jeu dans l'autre slot ; `--boot-game` démarre dessus.
- Toutes les options peuvent être mises dans `aka-emu.cfg` (une ligne `cle=valeur`, ex. `sd=./macarte`).
- Touches : flèches/ZQSD… (voir boutons à l'écran), IJKL = joystick, **F5** relancer, **F4** quitter.
- Si QEMU s'arrête ou est introuvable, le message apparaît dans le titre de la fenêtre.

## Performance
Compiler QEMU en *release* (`--disable-debug-info --enable-lto --extra-cflags=-O3`) : environ +7 % par rapport au build debug.
Sur un petit CPU 2 coeurs, pAKAman tourne à ~58 i/s (cible 60) ; le goulot est la traduction TCG de l'ESP32-S3
(un seul thread vCPU, `-accel tcg,thread=single`) — un PC récent est nettement plus à l'aise.
