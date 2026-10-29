#!/usr/bin/env python3
"""Generate the Galar (810-898) + Paldea (906-1025) expansion for TamaPoke.

Extends dex.h (DEX_TBL + REGIONS), moves.h (learnsets) and noart.h in place,
following the exact conventions of the existing generated tables:

- rows 1..809 are kept BYTE-IDENTICAL except the documented cross-generation
  evolution links (old mons whose evolvesTo is 0 gain a gen-8/9 target)
- Hisui species 899-905 are out of the game (deliberate: regional forms are not
  separate species here); their slots exist only so Paldea's numbers stay
  positional, exactly like the "?" slot 0
- rarity: legendary/mythical -> R_LEGENDARIO; evolution target -> R_EVO;
  base form -> R_COMUN, or R_RARO for starters (Kalos precedent), pseudo-
  legendary bases, fossils, paradox and version-exclusive rares
- accent colour and biome follow the per-type constants mined from the
  existing table (rock/water -> beach is the one documented exception)

Usage: python3 gen_dex.py <pokedata.json> <project_dir> <noart_json>
"""
import json
import os
import re
import sys

POKEDATA, PROJ, NOART_JSON = sys.argv[1], sys.argv[2], sys.argv[3]

GALAR = list(range(810, 899))
PALDEA = list(range(906, 1026))
HISUI = list(range(899, 906))
NEW = GALAR + PALDEA

ACCENT = {
    'bug': '0x7CC4', 'dark': '0x5A47', 'dragon': '0x5A98', 'electric': '0xBCA1',
    'fairy': '0xC333', 'fighting': '0xA2A5', 'fire': '0xEA87', 'flying': '0x7C7A',
    'ghost': '0x6AD3', 'grass': '0x3C49', 'ground': '0xB447', 'ice': '0x4DB8',
    'normal': '0x8C4D', 'poison': '0x8A73', 'psychic': '0xD28F', 'rock': '0x9407',
    'steel': '0x6BF1', 'water': '0x4C98',
}
BIOME = {  # 0 pradera, 1 playa, 2 bosque, 3 volcan, 4 montana, 5 nieve
    'bug': 2, 'dark': 2, 'dragon': 1, 'electric': 0, 'fairy': 0, 'fighting': 0,
    'fire': 3, 'flying': 0, 'ghost': 0, 'grass': 2, 'ground': 4, 'ice': 5,
    'normal': 0, 'poison': 0, 'psychic': 0, 'rock': 4, 'steel': 4, 'water': 1,
}

def clean_name(name):
    n = name.upper().replace("'", '').replace(':', '').replace('-', ' ')
    if n == 'MR RIME':
        n = 'MR. RIME'
    return n

# curated RARO base forms: starters (Kalos precedent), pseudo bases (Dreepy,
# Frigibax), the four Galar fossils, Duraludon, and the paradox / rare
# version-exclusives. Perrserker, Runerigus and Clodsire are RARO because
# their real pre-evolutions are regional forms, not separate species here.
RARO_NAMES = {
    'grookey', 'scorbunny', 'sobble', 'sprigatito', 'fuecoco', 'quaxly',
    'dreepy', 'frigibax',
    'arctozolt', 'arctovish', 'dracozolt', 'dracovish', 'duraludon',
    'perrserker', 'runerigus', 'clodsire',
    'tatsugiri', 'flamigo',
    'great-tusk', 'scream-tail', 'brute-bonnet', 'flutter-mane', 'slither-wing',
    'sandy-shocks', 'iron-treads', 'iron-bundle', 'iron-moth', 'iron-hands',
    'iron-jugulis', 'iron-thorns', 'roaring-moon', 'iron-valiant',
    'walking-wake', 'iron-leaves', 'raging-bolt', 'gouging-fire',
    'iron-boulder', 'iron-crown',
}

data = json.load(open(POKEDATA))

# canonical species names from the cached species endpoints
sp_names = {}
for did in NEW:
    fn = os.path.join(os.path.dirname(POKEDATA), 'pokeapi',
                      'pokemon-species_%d_.json' % did)
    sp_names[str(did)] = json.load(open(fn))['name']

# --------------------------------------------------------------------------
# evolution links
# --------------------------------------------------------------------------

def evo_level(chain, a, b):
    def walk(n):
        if int(n['species']['url'].split('/')[-2]) == a:
            for c in n['evolves_to']:
                if int(c['species']['url'].split('/')[-2]) == b:
                    for d in c['evolution_details']:
                        ml = d.get('min_level')
                        if ml:
                            return int(ml)
                    return 30
        for c in n['evolves_to']:
            r = walk(c)
            if r is not None:
                return r
        return None
    r = walk(chain['chain'])
    return r if r else 30

def chain_edges(chain):
    edges = []
    def walk(n):
        a = int(n['species']['url'].split('/')[-2])
        for c in n['evolves_to']:
            edges.append((a, int(c['species']['url'].split('/')[-2])))
            walk(c)
    walk(chain['chain'])
    return edges

new_targets = set()
links = {}      # new dex -> (target, level); FIRST chain-ordered target wins
old_links = {}  # old dex -> (target, level)
for k, v in data.items():
    for a, b in chain_edges(v['evolution_chain']):
        if a in HISUI or b in HISUI:
            continue
        if b in NEW:
            new_targets.add(b)
        if (a in NEW) and (b in NEW):
            if a not in links:            # a branch keeps its first option
                links[a] = (b, evo_level(v['evolution_chain'], a, b))
        elif a <= 809 and b in NEW:
            if a not in old_links:
                old_links[a] = (b, evo_level(v['evolution_chain'], a, b))

print('new-mon links (%d):' % len(links),
      {k: v for k, v in sorted(links.items())})
print('old-mon links:', {k: v for k, v in sorted(old_links.items())})

# --------------------------------------------------------------------------
# learnsets (level-up + machine moves, mapped onto MOVE_TBL)
# --------------------------------------------------------------------------

src = open(os.path.join(PROJ, 'moves.h')).read()
mv_block = re.search(r'static const MoveEntry MOVE_TBL\[MOVE_COUNT\] = \{(.*?)\n\};',
                     src, re.S).group(1)
MOVE_IDS = []
for m in re.finditer(r'\{ "([^"]+)"', mv_block):
    MOVE_IDS.append(m.group(1))
assert len(MOVE_IDS) == 90
mv_index = {}
for i, name in enumerate(MOVE_IDS):
    key = re.sub(r'[^A-Z0-9]', '', name.upper())
    mv_index[key] = i

VG_ORDER = [  # newest first
    'scarlet-violet', 'the-indigo-disk', 'the-teal-mask',
    'crown-tundra', 'island-armor', 'sword-shield',
    'brilliant-diamond-and-shining-pearl', 'legends-arceus',
]
VG_RANK = {v: i for i, v in enumerate(VG_ORDER)}

def norm(s):
    return re.sub(r'[^A-Z0-9]', '', s.upper())

def build_learnset(did):
    """level-up + machine moves mapped onto MOVE_TBL; TMs first (level 0),
    then level-up ascending -- the order pet.cpp's level scan relies on."""
    fn = os.path.join(os.path.dirname(POKEDATA), 'pokeapi', 'pokemon_%d_.json' % did)
    p = json.load(open(fn))
    levelup = {}   # move key -> (vg, level)
    machine = []   # move keys in machine order
    for mv in p['moves']:
        key = norm(mv['move']['name'])
        if key not in mv_index:
            continue
        best = None
        for v in mv['version_group_details']:
            vg = v['version_group']['name']
            if vg not in VG_RANK:
                continue
            if v['move_learn_method']['name'] == 'level-up':
                lvl = v.get('level_learned_at') or 0
                if best is None or VG_RANK[vg] < best[0]:
                    best = (VG_RANK[vg], lvl)
            elif v['move_learn_method']['name'] == 'machine' and key not in machine:
                machine.append(key)
        if best is not None:
            prev = levelup.get(key)
            if prev is None or best[0] < prev[0]:
                levelup[key] = best
    out = []
    for key in machine[:8]:
        out.append((mv_index[key], 0))
    for key, (vg, lvl) in sorted(levelup.items(), key=lambda x: x[1][1]):
        out.append((mv_index[key], min(100, max(1, int(lvl)))))
    return out

# --------------------------------------------------------------------------
# read existing tables
# --------------------------------------------------------------------------

dex_src = open(os.path.join(PROJ, 'dex.h')).read()
tbl = re.search(r'(static const DexEntry DEX_TBL\[DEX_COUNT \+ 1\] = \{\n)(.*?)(\n\};)',
                dex_src, re.S)
rows = [l for l in tbl.group(2).split('\n') if l.strip().startswith('{')]
assert len(rows) == 810, len(rows)
row_pat = re.compile(r'^(\{ "[^"]+", )(\d+)(, )(\d+)(, )(R_\w+)(, )')

# apply old-mon links: rewrite evolvesTo/level in the existing rows
old_by_name = {}
for i, l in enumerate(rows):
    m = re.match(r'\{ "([^"]+)",', l)
    if m:
        old_by_name[m.group(1)] = i

def set_evo(row, target, level):
    return re.sub(r'^(\s*\{ "[^"]+", )\d+(, )\d+(, )',
                  lambda m: m.group(1) + str(target) + m.group(2) + str(level) + m.group(3),
                  row)

NAME_TO_OLD = {}  # old species name -> dex (from the cached old table)
old_dex = {}
for i, l in enumerate(rows):
    m = re.match(r'\{ "([^"]+)", (\d+), (\d+)', l)
    if m and i >= 1:
        old_dex[i] = (m.group(1), int(m.group(2)), int(m.group(3)))

# map old dex numbers via the species cache for cross-gen links
old_species_names = {}
for did in range(1, 810):
    fn = os.path.join(os.path.dirname(POKEDATA), 'pokeapi', 'pokemon-species_%d_.json' % did)
    if os.path.exists(fn):
        old_species_names[json.load(open(fn))['name']] = did

# table-applied targets: what the single evolvesTo field can actually reach.
# A chain target that no table row points at (APPLIN's second and third
# branches, PERRSERKER/CLODSIRE/RUNERIGUS whose pre-evolutions are regional
# forms this game does not model) becomes a standalone RARO hatchable instead
# of an orphaned R_EVO that can never be obtained.
table_targets = set(t for t, _ in links.values())
standalone_raro = set(t for t in new_targets if t not in table_targets)

# old mons whose NEW evolution requires a GALARIAN form this game does not
# model - the base forms do not evolve in the games at all. Same treatment as
# the branch conflicts below: keep the old row, the new target becomes a
# standalone RARO hatchable (the PERRSERKER/CLODSIRE/RUNERIGUS rule).
FORM_GATED_OLD = {83, 122, 222, 264}   # FARFETCHD, MR. MIME, CORSOLA, LINOONE
# -> SIRFETCHD 865, MR. RIME 866, CURSOLA 864, OBSTAGOON 862

applied = []
for a, (b, lvl) in sorted(old_links.items()):
    # rows[i] is dex i+1; only link mons whose evolvesTo is still 0 -- an old
    # mon that already evolves (MEOWTH->PERSIAN, WOOPER->QUAGSIRE, YAMASK->
    # COFAGRIGUS) keeps its existing target; the new branch species becomes a
    # standalone RARO hatchable instead, because its real pre-evolution is a
    # regional form that is not a separate species in this game.
    row = rows[a]   # rows[i] is dex i (slot 0 "?" included)
    cur = re.match(r'\s*\{ "[^"]+", (\d+), (\d+)', row)
    if a in FORM_GATED_OLD:
        print('kept %s (%d) -> no evolution (form-gated; %d standalone)' % (
            re.match(r'\s*\{ "([^"]+)"', row).group(1), a, b))
        continue
    if int(cur.group(1)) != 0:
        print('kept %s (%d) -> %d (branch conflict)' % (
            re.match(r'\s*\{ "([^"]+)"', row).group(1), a, int(cur.group(1))))
        continue
    rows[a] = set_evo(row, b, lvl)
    applied.append(b)
    table_targets.add(b)
    print('linked %s (%d) -> %d lvl %d' % (
        re.match(r'\s*\{ "([^"]+)"', rows[a]).group(1), a, b, lvl))

standalone_raro = set(t for t in standalone_raro if t not in table_targets)
print('standalone RARO (chain targets the table cannot reach):',
      sorted(standalone_raro))

# --------------------------------------------------------------------------
# build new rows
# --------------------------------------------------------------------------
TYPE_ENUM = {t: 'T_' + t.upper() for t in ACCENT}

def type_field(t):
    return TYPE_ENUM[t]

new_rows = {810 + i: None for i in range(0, 116)}  # not used; explicit below

lines_out = []
entries = {}   # dex -> row text
for did in NEW:
    v = data[str(did)]
    name = clean_name(sp_names[str(did)])
    t1 = v['types'][0]
    t2 = v['types'][1] if len(v['types']) > 1 else None
    accent = ACCENT[t1]
    biome = BIOME[t1]
    if t1 == 'rock' and t2 == 'water':
        biome = 1
    st = v['stats']
    # DexEntry field order: bHp, bAtk, bDef, bSpe, bSpA, bSpD (speed 4th!)
    stats = (st['hp'], st['attack'], st['defense'], st['speed'],
             st['special-attack'], st['special-defense'])
    if v['legendary'] or v['mythical']:
        rar = 'R_LEGENDARIO'
    elif did in standalone_raro:
        rar = 'R_RARO'      # chain target the single evolvesTo field cannot reach
    elif did in table_targets:
        rar = 'R_EVO'
    elif sp_names[str(did)] in RARO_NAMES:
        rar = 'R_RARO'
    else:
        rar = 'R_COMUN'
    if did in links:
        tgt, lvl = links[did]
    else:
        tgt, lvl = 0, 0
    t2f = type_field(t2) if t2 else 'T_NONE'
    comment = '%d %s' % (did, t1 + ('/' + t2 if t2 else ''))
    entries[did] = ('  { "%s", %d, %d, %s, %s, %d, %d, %d, %d, %d, %d, %d, %s, %s },  // %s'
                    % (name, tgt, lvl, rar, accent, *stats, biome,
                       type_field(t1), t2f, comment))

# Hisui placeholders: same shape as the "?" slot 0
for did in HISUI:
    entries[did] = ('  { "?", 0, 0, 0, 0x2946, 50, 50, 50, 50, 50, 50, 0 },  // %d: sin usar (Hisui, fuera del juego)'
                    % did)

# --------------------------------------------------------------------------
# write dex.h
# --------------------------------------------------------------------------
body = tbl.group(2)
existing_lines = body.split('\n')
# emit the (possibly re-linked) old rows at the first data line, skip the rest
out_lines = []
inserted = False
for l in existing_lines:
    if l.strip().startswith('{'):
        if not inserted:
            out_lines.extend(rows)
            inserted = True
        continue
    out_lines.append(l)
assert inserted
# append new entries (810..1025)
for did in range(810, 1026):
    out_lines.append(entries[did])

new_dex = dex_src
# NB: splice FIRST - the DEX_COUNT replace changes the string length and would
# shift every regex index if it ran before the slice.
new_dex = new_dex[:tbl.start(2)] + '\n'.join(out_lines) + new_dex[tbl.end(2):]
new_dex = new_dex.replace('#define DEX_COUNT 809', '#define DEX_COUNT 1025')

# REGIONS block
new_dex = new_dex.replace(
    'static const int16_t REGION_START_ALOLA[] = { 722, 725, 728 };',
    'static const int16_t REGION_START_ALOLA[] = { 722, 725, 728 };\n'
    'static const int16_t REGION_START_GALAR[] = { 810, 813, 816 };\n'
    'static const int16_t REGION_START_PALDEA[] = { 906, 909, 912 };')
new_dex = new_dex.replace(
    'static const int16_t REGION_START_ALL[] = { 1, 4, 7, 25, 133, 152, 155, 158, 252, 255, 258, 387, 390, 393, 495, 498, 501, 650, 653, 656, 722, 725, 728 };',
    'static const int16_t REGION_START_ALL[] = { 1, 4, 7, 25, 133, 152, 155, 158, 252, 255, 258, 387, 390, 393, 495, 498, 501, 650, 653, 656, 722, 725, 728, 810, 813, 816, 906, 909, 912 };')
new_dex = new_dex.replace('#define REGION_COUNT 8\n#define REGION_ALL 7',
                          '#define REGION_COUNT 10\n#define REGION_ALL 9')
new_dex = new_dex.replace(
    '  { "ALOLA", 722, 809, REGION_START_ALOLA, 3 },\n'
    '  { "ALL", 1, 809, REGION_START_ALL, 23 },',
    '  { "ALOLA", 722, 809, REGION_START_ALOLA, 3 },\n'
    '  { "GALAR", 810, 898, REGION_START_GALAR, 3 },\n'
    '  { "PALDEA", 906, 1025, REGION_START_PALDEA, 3 },\n'
    '  { "ALL", 1, 1025, REGION_START_ALL, 29 },')

# reachable-dex helpers (Hisui placeholders are neither obtainable nor counted)
new_dex = new_dex.replace(
    '#pragma once\n#include <stdint.h>',
    '''#pragma once
#include <stdint.h>

// Hisui species 899-905 are deliberately out of the game: their slots exist
// only to keep Paldea's numbers positional (dex numbers are positional in saved
// data), so they can never be registered and are not counted by the Pokedex.
#define DEX_UNREACHABLE_LO 899
#define DEX_UNREACHABLE_HI 905
#define DEX_REACHABLE (DEX_COUNT - (DEX_UNREACHABLE_HI - DEX_UNREACHABLE_LO + 1))
static inline bool dexSlotPlayable(int16_t d) {
  return d < DEX_UNREACHABLE_LO || d > DEX_UNREACHABLE_HI;
}''', 1)

open(os.path.join(PROJ, 'dex.h'), 'w').write(new_dex)
print('dex.h written')

# --------------------------------------------------------------------------
# write moves.h
# --------------------------------------------------------------------------
learn_tbl = re.search(r'(static const LearnEntry LEARN_TBL\[)(\d+)(\] = \{\n)(.*?)(\n\};)',
                      src, re.S)
old_entries_txt = learn_tbl.group(4)
old_learn = [l for l in old_entries_txt.split('\n')]
# extract entry tuples from existing lines
entry_pat = re.compile(r'\{\s*(\d+),\s*(\d+)\s*\}')
old_list = []
for l in old_learn:
    for m in entry_pat.finditer(l):
        old_list.append((int(m.group(1)), int(m.group(2))))
assert len(old_list) == 11114, len(old_list)

ofs_block = re.search(r'(static const uint16_t LEARN_OFS\[DEX_COUNT \+ 2\] = \{\n)(.*?)(\n\};)',
                      src, re.S)
ofs_list = [int(x) for x in re.findall(r'\d+', ofs_block.group(2))]
assert len(ofs_list) == 811, len(ofs_list)
assert ofs_list[810] == 11114

new_learn = list(old_list)
new_ofs = list(ofs_list)
# LEARN_OFS[n] is the start of species n; LEARN_OFS[810] == 11114 is already
# species 810's start, so the loop appends starts for 811..1025 and then the
# final sentinel LEARN_OFS[DEX_COUNT + 1] == len(LEARN_TBL).
for did in range(810, 1026):
    if did in HISUI:
        entries_l = []
    else:
        entries_l = build_learnset(did)
    if did >= 811:
        new_ofs.append(len(new_learn))
    for mv, lvl in entries_l:
        new_learn.append((mv, lvl))
new_ofs.append(len(new_learn))
assert len(new_ofs) == 1027, len(new_ofs)
assert new_ofs[1026] == len(new_learn), (new_ofs[1026], len(new_learn))

def fmt_entries(lst):
    out = []
    line = []
    for mv, lvl in lst:
        line.append('{ %3d, %3d },' % (mv, lvl))
        if len(line) == 6:
            out.append('  ' + ' '.join(line))
            line = []
    if line:
        out.append('  ' + ' '.join(line))
    return '\n'.join(out)

new_moves = src
# NB: splice before the count replace (count is length-preserving today, but do
# not rely on that).
new_moves = (new_moves[:learn_tbl.start(4)] + '\n' + fmt_entries(new_learn) +
             new_moves[learn_tbl.end(4):])
new_moves = new_moves.replace('static const LearnEntry LEARN_TBL[11114] = {',
                              'static const LearnEntry LEARN_TBL[%d] = {' % len(new_learn))
# rewrite LEARN_OFS block
def fmt_ofs(lst):
    out = []
    line = []
    for v in lst:
        line.append('%6d,' % v)
        if len(line) == 12:
            out.append('  ' + ' '.join(line))
            line = []
    if line:
        out.append('  ' + ' '.join(line))
    return '\n'.join(out)
ofs_block2 = re.search(r'(static const uint16_t LEARN_OFS\[DEX_COUNT \+ 2\] = \{\n)(.*?)(\n\};)',
                       new_moves, re.S)
new_moves = (new_moves[:ofs_block2.start(2)] + fmt_ofs(new_ofs) +
             new_moves[ofs_block2.end(2):])
open(os.path.join(PROJ, 'moves.h'), 'w').write(new_moves)
print('moves.h written: %d learn entries' % len(new_learn))

# --------------------------------------------------------------------------
# write noart.h
# --------------------------------------------------------------------------
noart = json.load(open(NOART_JSON))
cur = re.search(r'#define NO_ART_COUNT (\d+)\nstatic const int16_t NO_ART\[NO_ART_COUNT \? NO_ART_COUNT : 1\] = \{\n(.*?)\};',
                open(os.path.join(PROJ, 'noart.h')).read(), re.S)
old_list_n = [int(x) for x in re.findall(r'\d+', cur.group(2))]
merged = sorted(set(old_list_n) | set(noart))
noart_src = open(os.path.join(PROJ, 'noart.h')).read()
noart_src = noart_src.replace('#define NO_ART_COUNT %d' % int(cur.group(1)),
                              '#define NO_ART_COUNT %d' % len(merged))
noart_src = re.sub(r'(static const int16_t NO_ART\[NO_ART_COUNT \? NO_ART_COUNT : 1\] = \{\n).*?(\};)',
                   lambda m: m.group(1) + '  ' + ', '.join(str(x) for x in merged) + '\n' + m.group(2),
                   noart_src, flags=re.S)
open(os.path.join(PROJ, 'noart.h'), 'w').write(noart_src)
print('noart.h written: %d entries (was %d)' % (len(merged), len(old_list_n)))
