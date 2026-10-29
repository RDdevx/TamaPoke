#!/usr/bin/env python3
"""Unpacks the region sprite packs (web/sprites-*.pak) into a Mon folder.

The emulator loads sprites from a plain directory (default: tools/sdcard/mons,
overridable with --sprites when running it). That folder is empty in a fresh
checkout because the sprites only exist inside the packs -- so without this
step the emulator shows the crude built-in fallback art and the pet never
walks. This writes every file from every pack's Mon folder into one Mon
folder, the same files the web installer would put on the SD card.

    python3 tools/unpack_paks.py                 # web/*.pak -> tools/sdcard/mons
    python3 tools/unpack_paks.py -o my-mons      # somewhere else
    python3 tools/unpack_paks.py web/sprites-kanto.pak   # just one pack

Safe to re-run: files are simply overwritten.
"""
import argparse
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def parse_pak(path):
    with open(path, "rb") as f:
        buf = f.read()
    if buf[:4] != b"TPAK":
        raise SystemExit(f"{path}: not a TPAK bundle")
    count = struct.unpack("<H", buf[4:6])[0]
    off = 6
    table = []
    for _ in range(count):
        nl = buf[off]
        off += 1
        name = buf[off : off + nl].decode()
        off += nl
        size = struct.unpack("<I", buf[off : off + 4])[0]
        off += 4
        table.append((name, size))
    pos = off
    for name, size in table:
        yield name, buf[pos : pos + size]
        pos += size


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paks", nargs="*", help="pack files (default: web/sprites-*.pak)")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "tools", "sdcard", "mons"),
                    help="output Mon folder (default: tools/sdcard/mons)")
    args = ap.parse_args()

    paks = args.paks or sorted(glob.glob(os.path.join(ROOT, "web", "sprites-*.pak")))
    if not paks:
        raise SystemExit("no packs found -- run this from the project root, or pass pack files")
    os.makedirs(args.out, exist_ok=True)

    total = 0
    for pak in paks:
        n = 0
        for name, data in parse_pak(pak):
            dest = os.path.join(args.out, os.path.relpath(name, "mons"))
            if name.startswith("mons/"):
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as f:
                    f.write(data)
                n += 1
                total += 1
        print(f"{os.path.basename(pak)}: {n} files")
    print(f"done: {total} files -> {args.out}")
    print("run the emulator against it:  ./tools/emu/tamapoke-emu --sprites " + args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
