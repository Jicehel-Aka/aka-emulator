# Piste Android

Faisable, mais c'est un vrai chantier (QEMU TCG tourne sur ARM64, pas de KVM nécessaire) :
1. **QEMU en bibliothèque native** : cross-compiler `qemu-system-xtensa` avec le NDK (aarch64-linux-android),
   glib/pixman/zlib compilés via meson subprojects. Livré dans `jniLibs/arm64-v8a/libqemu-xtensa.so`
   et lancé comme sous-processus (Android autorise l'exécution depuis `nativeLibraryDir`).
2. **Front-end** : le code SDL2 actuel se réutilise tel quel via le template SDL2 Android (Gradle + NDK).
   Remplacer fork/exec par `posix_spawn` (déjà isolé dans `front/platform.h`) et le répertoire partagé par
   `getCacheDir()` (mmap fonctionne hors `/dev/shm`).
3. **Interface** : boutons tactiles (superpositions SDL_FINGER*), joystick virtuel, accès à la carte SD
   simulée dans le stockage de l'app (SAF pour importer des jeux), son SDL déjà géré.
4. **Perf** : l'ESP32-S3 émulé en TCG demande un SoC récent (≥ Snapdragon 8 Gen 1 pour approcher le temps réel) ; à valider.
Étape suivante possible : cross-compiler QEMU avec le NDK pour mesurer la vitesse.
