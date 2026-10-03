#!/usr/bin/env python3
"""
aka_flash.py - assemble une image de flash 8 Mo comme le ferait la console AKA.

Principe : aucun offset n'est ecrit en dur. Tout vient de la table de
partitions (CSV ou .bin), exactement comme le font l'AKA et ESP-IDF :
  - loader : premiere partition app de sous-type OTA_1 (cf. aka_runtime.cpp)
  - jeu    : premiere partition app de sous-type OTA_0, sinon FACTORY
  - otadata: ecrite pour choisir la partition de demarrage (launcher ou jeu)

Usage :
  aka_flash.py --partitions parts.csv|parts.bin --bootloader bootloader.bin \
      [--loader launcher.bin] [--game firmware.bin] [--boot loader|game] \
      [--flash-size 8] -o flash.bin
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

IDF = os.environ.get("IDF_PATH", os.path.expanduser("~/aka-emu/esp-idf"))
sys.path.insert(0, os.path.join(IDF, "components", "partition_table"))
import gen_esp32part as gp  # noqa: E402

BOOTLOADER_OFFSET = 0x0       # ESP32-S3
PARTTABLE_OFFSET = 0x8000
SECTOR = 0x1000


def load_table(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] == b"\xaa\x50":
        return gp.PartitionTable.from_binary(raw)
    return gp.PartitionTable.from_csv(raw.decode())


def find_app(table, subtypes):
    for sub in subtypes:
        for p in table:
            if p.type == gp.APP_TYPE and p.subtype == sub:
                return p
    return None


def patch_bootloader(path, flash_size_mb):
    """Reecrit l'en-tete (taille de flash) et le hash comme esptool merge_bin."""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "bl.bin")
        subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin",
             "--flash_mode", "dio", "--flash_freq", "80m",
             "--flash_size", "%dMB" % flash_size_mb, "-o", out, "0x0", path],
            check=True, stdout=subprocess.DEVNULL)
        return open(out, "rb").read()


def ota_select_entry(seq):
    """esp_ota_select_entry_t : seq, label[20], state, crc."""
    body = struct.pack("<I", seq)
    crc = zlib.crc32(body, 0xFFFFFFFF) & 0xFFFFFFFF
    return body + b"\xff" * 20 + struct.pack("<II", 0xFFFFFFFF, crc)


def put(img, offset, data, what):
    if offset + len(data) > len(img):
        sys.exit("%s depasse la flash (0x%x + 0x%x)" % (what, offset, len(data)))
    img[offset:offset + len(data)] = data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--partitions", required=True)
    ap.add_argument("--bootloader", required=True)
    ap.add_argument("--loader")
    ap.add_argument("--game")
    ap.add_argument("--boot", choices=["loader", "game"], default="loader")
    ap.add_argument("--flash-size", type=int, default=8, help="en Mo")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()

    table = load_table(a.partitions)
    img = bytearray(b"\xff" * (a.flash_size * 1024 * 1024))

    put(img, BOOTLOADER_OFFSET, patch_bootloader(a.bootloader, a.flash_size),
        "bootloader")
    put(img, PARTTABLE_OFFSET, table.to_binary(), "table de partitions")

    loader = find_app(table, [gp.SUBTYPES[gp.APP_TYPE]["ota_1"]])
    game = find_app(table, [gp.SUBTYPES[gp.APP_TYPE]["ota_0"],
                            gp.SUBTYPES[gp.APP_TYPE]["factory"]])
    if a.loader:
        if not loader:
            sys.exit("pas de partition loader (app/ota_1) dans la table")
        data = open(a.loader, "rb").read()
        if len(data) > loader.size:
            sys.exit("le launcher ne tient pas dans %s" % loader.name)
        put(img, loader.offset, data, "launcher")
        print("launcher -> %-8s @0x%06x" % (loader.name, loader.offset))
    if a.game:
        if not game:
            sys.exit("pas de partition jeu (app/ota_0 ou factory)")
        data = open(a.game, "rb").read()
        if len(data) > game.size:
            sys.exit("le jeu ne tient pas dans %s" % game.name)
        put(img, game.offset, data, "jeu")
        print("jeu      -> %-8s @0x%06x" % (game.name, game.offset))

    otadata = next((p for p in table if p.type == gp.DATA_TYPE
                    and p.subtype == gp.SUBTYPES[gp.DATA_TYPE]["ota"]), None)
    if otadata:
        want = loader if a.boot == "loader" else game
        ota_parts = [p for p in table if p.type == gp.APP_TYPE and
                     gp.SUBTYPES[gp.APP_TYPE]["ota_0"] <= p.subtype <
                     gp.SUBTYPES[gp.APP_TYPE]["ota_0"] + 16]
        ota_parts.sort(key=lambda p: p.subtype)
        if want in ota_parts:
            idx = ota_parts.index(want)
            # slot = (seq - 1) % n  ->  seq = idx + 1 (premiere valeur valide)
            seq = idx + 1
            put(img, otadata.offset, ota_select_entry(seq), "otadata")
            print("otadata  -> demarrage sur %s (seq=%d)" % (want.name, seq))
        else:
            print("otadata  -> laissee vide (demarrage sur factory/ota_0)")
    else:
        print("pas d'otadata : demarrage sur factory/ota_0")

    with open(a.output, "wb") as f:
        f.write(img)
    print("ecrit %s (%d Mo)" % (a.output, a.flash_size))


if __name__ == "__main__":
    main()
