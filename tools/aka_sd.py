#!/usr/bin/env python3
"""
aka_sd.py - cree une image de carte SD (FAT32) pour l'emulateur AKA.

  aka_sd.py -o sd.img --size 128 --app PAKAMAN=firmware.bin [--meta PAKAMAN=meta.json]
            [--screen PAKAMAN=screen.bpm] [--tree dossier] [--label AKA]

Une application = un dossier a la racine avec firmware.bin (+ meta.json, screen.bpm).
--tree copie en plus le contenu d'un dossier tel quel a la racine (ex. AKA/lang).
La taille doit etre une puissance de 2 en Mo (exigence de l'emulation SD de QEMU).
"""
import argparse, os, subprocess, sys, tempfile, shutil

def run(*a):
    subprocess.run(a, check=True, stdout=subprocess.DEVNULL)

def kv(items):
    out = {}
    for it in items or []:
        k, _, v = it.partition("=")
        out[k] = v
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--size", type=int, default=128, help="Mo, puissance de 2")
    ap.add_argument("--label", default="AKA")
    ap.add_argument("--app", action="append", help="DOSSIER=firmware.bin")
    ap.add_argument("--meta", action="append", help="DOSSIER=meta.json")
    ap.add_argument("--screen", action="append", help="DOSSIER=screen.bpm")
    ap.add_argument("--tree", action="append", help="dossier copie a la racine")
    a = ap.parse_args()
    if a.size & (a.size - 1):
        sys.exit("--size doit etre une puissance de 2")

    with open(a.output, "wb") as f:
        f.truncate(a.size * 1024 * 1024)
    run("mkfs.vfat", "-F", "32", "-n", a.label[:11], a.output)

    stage = tempfile.mkdtemp()
    try:
        for folder, fw in kv(a.app).items():
            d = os.path.join(stage, folder); os.makedirs(d, exist_ok=True)
            shutil.copy(fw, os.path.join(d, "firmware.bin"))
        for folder, p in kv(a.meta).items():
            os.makedirs(os.path.join(stage, folder), exist_ok=True)
            shutil.copy(p, os.path.join(stage, folder, "meta.json"))
        for folder, p in kv(a.screen).items():
            os.makedirs(os.path.join(stage, folder), exist_ok=True)
            shutil.copy(p, os.path.join(stage, folder, "screen.bpm"))
        for t in a.tree or []:
            shutil.copytree(t, stage, dirs_exist_ok=True)
        entries = [os.path.join(stage, e) for e in os.listdir(stage)]
        if entries:
            run("mcopy", "-i", a.output, "-s", "-m", *entries, "::/")
    finally:
        shutil.rmtree(stage)
    print("ecrit %s (%d Mo)" % (a.output, a.size))

if __name__ == "__main__":
    main()
