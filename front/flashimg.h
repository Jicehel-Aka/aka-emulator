// flashimg.h - assemble une image de flash comme le ferait la console AKA :
// aucun offset en dur, tout vient de la table de partitions (partitions.bin).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace flashimg {

struct Partition {
    std::string name;
    uint8_t type = 0, subtype = 0;
    uint32_t offset = 0, size = 0;
};

struct Options {
    std::string partitions;      // partitions.bin (table binaire compilee)
    std::string bootloader;      // bootloader.bin
    std::string launcher;        // optionnel
    std::string game;            // optionnel
    int  launcherSlot = 1;       // 0 = app0 (ota_0), 1 = app1 (ota_1) : comme l'AKA
    bool bootGame = false;       // demarrer sur le jeu plutot que le launcher
    int  flashMB = 8;
};

// Retourne true et remplit `out` ; sinon `err` explique.
bool build(const Options& o, std::vector<uint8_t>& out, std::string& err);
bool loadTable(const std::string& path, std::vector<Partition>& t, std::string& err);

}  // namespace flashimg
