#!/usr/bin/env python3
"""Verifie une image applicative ESP32 (firmware.bin) comme le fait esp_ota_end().

  python aka_checkimg.py firmware.bin                 -> verifie l'image
  python aka_checkimg.py firmware.bin app0_dump.bin   -> + compare avec ce qui est en flash
      (dump : esptool.py --chip esp32s3 read_flash 0x10000 0x330000 app0_dump.bin)
Codes : 0 = image valide, 1 = invalide, 2 = difference avec la flash.
"""
import hashlib, struct, sys

def check(path):
    d = open(path, 'rb').read()
    ok = True
    def bad(msg):
        nonlocal ok; ok = False; print("  [ERREUR]", msg)
    print(f"{path} : {len(d)} octets")
    if len(d) < 24: bad("fichier trop court"); return False
    magic, nseg, mode, sz, entry = struct.unpack_from('<BBBBI', d, 0)
    chip = struct.unpack_from('<H', d, 12)[0]
    hash_app = d[23]
    print(f"  magic=0x{magic:02X} segments={nseg} entry=0x{entry:08X} chip_id=0x{chip:04X} hash_appended={hash_app}")
    if magic != 0xE9: bad("magic != 0xE9 (ce n'est pas une image applicative)")
    if chip != 0x0009: print("  [attention] chip_id != ESP32-S3 (0x0009)")
    off = 24; xor = 0xEF
    for i in range(nseg):
        if off + 8 > len(d): bad(f"segment {i} : en-tete tronque a l'offset {off}"); return False
        addr, ln = struct.unpack_from('<II', d, off); off += 8
        if off + ln > len(d): bad(f"segment {i} : donnees tronquees (attendu {ln}, reste {len(d)-off}) -> fichier incomplet"); return False
        for b in d[off:off+ln]: xor ^= b
        print(f"  segment {i}: addr=0x{addr:08X} len={ln}")
        off += ln
    pad = (16 - ((off + 1) % 16)) % 16
    cks_pos = off + pad
    if cks_pos >= len(d): bad("octet de somme de controle absent (fichier tronque)"); return False
    if d[cks_pos] != xor: bad(f"somme de controle: attendu 0x{xor:02X}, trouve 0x{d[cks_pos]:02X} -> donnees alterees")
    else: print("  somme de controle OK")
    end = cks_pos + 1
    if hash_app:
        if end + 32 > len(d): bad("SHA-256 final absent (fichier tronque)"); return False
        want = d[end:end+32]; got = hashlib.sha256(d[:end]).digest()
        if want != got: bad("SHA-256 incorrect -> image modifiee/alteree apres sa generation (ou copie corrompue)")
        else: print("  SHA-256 OK")
        end += 32
    print(f"  longueur d'image = {end} octets ; octets en trop dans le fichier = {len(d)-end}")
    print("  => " + ("IMAGE VALIDE" if ok else "IMAGE INVALIDE"))
    return ok

def compare(path, dump):
    a = open(path, 'rb').read(); b = open(dump, 'rb').read(len(a))
    if len(b) < len(a): print(f"  dump plus court que le fichier ({len(b)} < {len(a)})"); return False
    if a == b: print("  flash == fichier : les donnees ecrites sont identiques"); return True
    i = next(k for k in range(len(a)) if a[k] != b[k])
    n = sum(1 for k in range(len(a)) if a[k] != b[k])
    print(f"  DIFFERENCE : 1er octet different a l'offset 0x{i:X} ({i}), {n} octets differents au total")
    print(f"  (fichier=0x{a[i]:02X}, flash=0x{b[i]:02X})")
    return False

if __name__ == '__main__':
    if len(sys.argv) < 2: print(__doc__); sys.exit(1)
    r = check(sys.argv[1]); rc = 0 if r else 1
    if len(sys.argv) > 2 and not compare(sys.argv[1], sys.argv[2]): rc = 2
    sys.exit(rc)
