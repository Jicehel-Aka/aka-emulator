# Patch QEMU
`aka-qemu.patch` s'applique sur le fork Espressif `https://github.com/espressif/qemu`, commit
`febae182e132e4055529be423a818225ebddaa3a` (QEMU 9.2.2) : `git apply qemu-patch/aka-qemu.patch`.
Il ajoute les périphériques AKA (LCD_CAM/écran, expander I2C, ADC/SENS, I2S) et corrige le cache flash / GDMA / SHA / eFuse, émule le watchdog RTC (reset cause 0x10 : retour au Launcher dans la disposition loader/game) et garde le cache de flash et les traductions TCG cohérents après une réécriture de la flash par l'application (installation d'un jeu ou du Launcher).
