#!/usr/bin/env python3
"""Fetch PMD SpriteCollab sprites for Galar/Paldea species and build TPK2
binaries, the 1025-entry thumbs.bin, and the region TPAK bundles.

Replaces the upstream tools/pack_pmd.py + make_thumbs.py + pack_bundle.py
pipeline for the new regions (those tools were not available to re-run).

SpriteCollab layout:
  sprite/<dex>/                    default form sprites (AnimData.xml + PNGs)
  sprite/<dex>/<form>/<shiny>/     shiny variant of a form
  sprite/<dex>/<form>/             alternate form that has the walking art

TPK2 (matches sdmon.cpp PmdMon::load):
  "TPK2" | nActs u8 | palCount u16le | pal RGB565 u16le * palCount |
  per act: id u8, w u8, h u8, nf u8, ms u16le * nf, idx (w*h*nf), 0xFF = transparent

Frame timing follows the original packs: ms = max(70, round(dur * 70 / 3)).
"""
import json
import os
import re
import struct
import sys
import urllib.request

RAW = 'https://raw.githubusercontent.com/PMDCollab/SpriteCollab/master'
CACHE = 'cache/sc'
OUT = 'out'

# TPK2 action id -> SpriteCollab anim name
ACTS = [
    (0, 'Idle'),
    (1, 'Walk'),      # walk left: raw sheet (PMD sprites face left)
    (2, 'Walk'),      # walk right: mirrored
    (3, 'Sleep'),
    (4, 'Eat'),
    (5, 'Hurt'),
    (6, 'Attack'),
    (7, 'Pose'),
    (8, 'Hop'),
    (9, 'Nod'),
    (10, 'DeepBreath'),
    (11, 'Sit'),
]


def http_get(url, binary=True):
    fn = os.path.join(CACHE, url.replace(RAW + '/', '').replace('/', '__'))
    if os.path.exists(fn):
        return open(fn, 'rb').read()
    for attempt in range(5):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'TamaPoke-pakgen/1.0'})
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
            os.makedirs(CACHE, exist_ok=True)
            open(fn, 'wb').write(data)
            return data
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            raise
        except Exception as e:
            print('retry', url, e)
    raise RuntimeError('failed ' + url)


def parse_animdata(xml_bytes):
    """Return {name: {'index':int,'fw':int,'fh':int,'durs':[int]}}"""
    txt = xml_bytes.decode('utf-8', 'replace')
    anims = {}
    for m in re.finditer(
            r'<Name>(\w+)</Name>\s*<Index>(\d+)</Index>\s*'
            r'<FrameWidth>(\d+)</FrameWidth>\s*<FrameHeight>(\d+)</FrameHeight>(.*?)</Anim>',
            txt, re.S):
        name, idx, fw, fh, rest = m.groups()
        durs = [max(0, int(d)) for d in re.findall(r'<Duration>(-?\d+)</Duration>', rest)]
        anims[name] = {'index': int(idx), 'fw': int(fw), 'fh': int(fh), 'durs': durs}
    return anims


def frame_ms(dur):
    return max(70, round(dur * 70 / 3))


def load_tracker():
    fn = os.path.join(CACHE, 'tracker.json')
    if not os.path.exists(fn):
        data = http_get(RAW + '/tracker.json')
        open(fn, 'wb').write(data)
    return json.load(open(fn))


def resolve_paths(tracker, dex):
    """Return (normal_dir, shiny_dir or None) using tracker subgroup info.

    Probes raw URLs; falls back across forms. Returns directories *relative*
    under sprite/, e.g. ('sprite/0810', 'sprite/0810/0000/0001').
    """
    d4 = '%04d' % dex
    ent = tracker.get(d4)
    forms = list(ent['subgroups'].keys()) if ent else ['0000']
    if '0000' not in forms:
        forms = ['0000'] + forms
    # normal: prefer sprite/<dex>/, then sprite/<dex>/<form>/
    normal = None
    for f in forms:
        cand = 'sprite/' + d4 if f == '0000' else 'sprite/%s/%s' % (d4, f)
        if http_get('%s/%s/AnimData.xml' % (RAW, cand)):
            normal = cand
            break
    if normal is None:
        return None, None
    # shiny: sprite/<dex>/<form>/<shinykey>/
    shiny = None
    if ent:
        for f in forms:
            sub = ent['subgroups'].get(f, {})
            for sk, sv in sub.get('subgroups', {}).items():
                if sv.get('name') == 'Shiny':
                    cand = 'sprite/%s/%s/%s' % (d4, f, sk)
                    if http_get('%s/%s/AnimData.xml' % (RAW, cand)):
                        shiny = cand
                        break
            if shiny:
                break
    return normal, shiny


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def build_mon(dex, scdir):
    """Fetch + assemble the TPK2 blob for one species. Returns bytes or None."""
    xml = http_get('%s/%s/AnimData.xml' % (RAW, scdir))
    if not xml:
        return None
    anims = parse_animdata(xml)
    if not anims:
        return None
    from PIL import Image
    import io

    # 1) gather frames per action
    actions = {}   # act_id -> list of (w, h, rows[][]) of RGBA tuples
    for act_id, name in ACTS:
        if name not in anims:
            continue
        a = anims[name]
        nf = len(a['durs'])
        if nf == 0 or a['fw'] == 0 or a['fh'] == 0 or nf > 24:
            continue
        png = http_get('%s/%s/%s-Anim.png' % (RAW, scdir, name))
        if not png:
            continue
        img = Image.open(io.BytesIO(png)).convert('RGBA')
        if img.width < a['fw'] * nf or img.height < a['fh']:
            continue
        frames = []
        ok = True
        for k in range(nf):
            fr = img.crop((k * a['fw'], 0, (k + 1) * a['fw'], a['fh']))
            if act_id == 2:  # walk right: mirror
                fr = fr.transpose(Image.FLIP_LEFT_RIGHT)
            frames.append(fr)
        actions[act_id] = (a, frames)

    if 0 not in actions and 1 not in actions:
        return None  # needs idle or walk at minimum

    # 2) shared palette: idle frame 0 colors first (keeps thumbs.bin trivial),
    #    then everything else in first-appearance order
    def frame_colors(fr):
        cols = []
        seen = set()
        for px in fr.getdata():
            if px[3] < 128:
                continue
            c = px[:3]
            if c not in seen:
                seen.add(c)
                cols.append(c)
        return cols

    order = []
    seen = set()

    def add_colors(cols):
        for c in cols:
            if c not in seen:
                seen.add(c)
                order.append(c)

    ref_act = 0 if 0 in actions else 1
    ref_frames = actions[ref_act][1]
    add_colors(frame_colors(ref_frames[0]))
    if len(ref_frames) > 1:
        for fr in ref_frames[1:]:
            add_colors(frame_colors(fr))
    for act_id in sorted(actions):
        if act_id == ref_act:
            continue
        for fr in actions[act_id][1]:
            add_colors(frame_colors(fr))

    if len(order) > 255:
        # too many colors: quantize. Rare; PMD art is 15 colours.
        return None

    pal_idx = {c: i for i, c in enumerate(order)}
    pal565 = [rgb565(*c) for c in order]

    # 3) emit
    out = bytearray()
    out += b'TPK2'
    out.append(len(actions))
    out += struct.pack('<H', len(pal565))
    for p in pal565:
        out += struct.pack('<H', p)
    for act_id in sorted(actions):
        a, frames = actions[act_id]
        w, h = a['fw'], a['fh']
        out += bytes([act_id, w, h, len(frames)])
        for d in a['durs'][:len(frames)]:
            out += struct.pack('<H', frame_ms(d))
        for fr in frames:
            for px in fr.getdata():
                if px[3] < 128:
                    out.append(0xFF)
                else:
                    out.append(pal_idx[px[:3]])
    return bytes(out), actions, ref_act, pal_idx, order


def thumb_from_mon(blob, dex):
    """Derive the TPTH blob entry from a TPK2 blob: idle (or walk) frame 0."""
    # re-derive: use the same frame-0-first palette the packer used
    nActs = blob[4]
    palCount = struct.unpack('<H', blob[5:7])[0]
    p = 7 + palCount * 2
    acts = {}
    while p < len(blob):
        aid, w, h, nf = blob[p], blob[p+1], blob[p+2], blob[p+3]
        p += 4
        p += nf * 2
        data = blob[p:p + w * h * nf]
        p += w * h * nf
        acts[aid] = (w, h, nf, data)
    ref = 0 if 0 in acts else 1
    w, h, nf, data = acts[ref]
    f0 = data[:w * h]
    used = sorted(set(v for v in f0 if v != 0xFF))
    # palette indices in the blob are already frame-0-first, so thumb palette
    # is the first len(used) entries; indices need no remap
    n = used[-1] + 1
    pal = blob[7:7 + n * 2]
    return bytes([w, h, n]) + pal + f0


def main():
    os.makedirs(OUT, exist_ok=True)
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--range', required=True, help='e.g. 810-898 or 906-1025')
    ap.add_argument('--name', required=True, help='region slug for the pak')
    ap.add_argument('--thumbs-from', default=None, metavar='PAK',
                    help='pak whose thumbs.bin to extend (default: web/sprites-alola.pak). '
                         'Pass the other new region\'s pak so BOTH new paks ship the complete '
                         'merged thumbs - /mons/thumbs.bin is a single overwritten file on the '
                         'card, so each pak must carry every region\'s thumbnails.')
    args = ap.parse_args()
    lo, hi = (int(x) for x in args.range.split('-'))

    tracker = load_tracker()

    # thumbs.bin: start from the shipped 809-entry one, extend to 1025
    def parse_pak(path):
        d = open(path, 'rb').read()
        assert d[:4] == b'TPAK'
        count = struct.unpack('<H', d[4:6])[0]
        off = 6
        items = []
        for i in range(count):
            nl = d[off]; off += 1
            name = d[off:off+nl].decode(); off += nl
            size = struct.unpack('<I', d[off:off+4])[0]; off += 4
            items.append((name, size))
        pos = off
        out = []
        for name, size in items:
            out.append((name, d[pos:pos+size]))
            pos += size
        return out

    # thumbs.bin for older dex numbers are copied from another pak. Default is
    # the shipped Alola pack (the old full 1-809 set); pass --thumbs-from to
    # chain builds so every new pak ships the complete merged set.
    if args.thumbs_from:
        alola_pak = args.thumbs_from
    else:
        alola_pak = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 '..', '..', 'web', 'sprites-alola.pak')
        if not os.path.exists(alola_pak):
            alola_pak = os.path.join('web', 'sprites-alola.pak')   # run from project root
    pak_items = parse_pak(alola_pak)
    old_thumbs = dict(pak_items)['mons/thumbs.bin']
    tc = struct.unpack('<H', old_thumbs[4:6])[0]
    toffs = struct.unpack('<%dI' % tc, old_thumbs[6:6+4*tc])
    old_entries = []
    for i in range(tc):
        off = toffs[i]
        end = toffs[i+1] if i + 1 < tc else len(old_thumbs)
        old_entries.append(old_thumbs[off:end])
    print('old thumbs:', len(old_entries))

    entries = {}       # pak path -> bytes
    # new_thumbs must end up with exactly DEX_COUNT (1025) slots. A chained
    # --thumbs-from pak already has 1025; the Alola default has 809 and is
    # padded with empty placeholders below.
    if len(old_entries) == 1025:
        new_thumbs = list(old_entries)
    else:
        assert len(old_entries) == 809, len(old_entries)
        new_thumbs = list(old_entries)  # dex 1..809
        for dex in range(810, 1026):
            new_thumbs.append(bytes([0, 0, 0]))  # placeholder for missing art

    no_art = []
    for dex in range(lo, hi + 1):
        normal, shiny = resolve_paths(tracker, dex)
        if normal is None:
            no_art.append(dex)
            print('%04d: NO ART' % dex)
            continue
        res = build_mon(dex, normal)
        if res is None:
            no_art.append(dex)
            print('%04d: pack failed' % dex)
            continue
        blob = res[0]
        entries['mons/p%03d.bin' % dex] = blob
        new_thumbs[dex - 1] = thumb_from_mon(blob, dex)
        print('%04d: %6.1f KB' % (dex, len(blob) / 1024))
        if shiny:
            res = build_mon(dex, shiny)
            if res:
                entries['mons/ps%03d.bin' % dex] = res[0]
                print('      shiny: %6.1f KB' % (len(res[0]) / 1024))
            else:
                print('      shiny: pack failed')

    # build new thumbs.bin (count 1025)
    t = bytearray(b'TPTH')
    t += struct.pack('<H', 1025)
    offs = []
    body = bytearray()
    for e in new_thumbs[:1025]:
        offs.append(6 + 4 * 1025 + len(body))
        body += e
    t += struct.pack('<1025I', *offs)
    t += body
    entries['mons/thumbs.bin'] = bytes(t)

    # TPAK bundle
    def key(path):
        if path.endswith('thumbs.bin'):
            return (2, 0)
        if '/ps' in path:
            return (1, int(path.split('ps')[1].split('.')[0]))
        return (0, int(path.split('p')[1].split('.')[0]))
    out = bytearray(b'TPAK')
    out += struct.pack('<H', len(entries))
    for name in sorted(entries, key=key):
        out.append(len(name))
        out += name.encode()
        out += struct.pack('<I', len(entries[name]))
    for name in sorted(entries, key=key):
        out += entries[name]
    fn = os.path.join(OUT, 'sprites-%s.pak' % args.name)
    open(fn, 'wb').write(out)
    print('wrote %s (%.1f MB), %d mons, no-art: %s' % (
        fn, len(out) / 1048576, len([k for k in entries if '/p' in k and 'ps' not in k]), no_art))


if __name__ == '__main__':
    main()
