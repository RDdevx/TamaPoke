# Galar + Paldea update — install notes

Extract this zip over the project root (the paths below are relative to it).
It touches only the listed files; your local `tools/` folder is not replaced —
the new scripts live in `tools/galar_paldea/`.

## Files

```
dex.h  moves.h  noart.h        dex 810-1025, learnsets, no-art list
pet.cpp  save.cpp  save.h      region migration + save v2
TamaPoke.ino                    dex displays use DEX_REACHABLE
web/index.html                  galar + paldea in REGION_PAKS
web/sprites-galar.pak           84 mons, ~25 MB
web/sprites-paldea.pak          102 mons, ~30 MB
tools/galar_paldea/             the pipeline that generated all of the above
tools/build_web.py              build_web.sh's step in Python (no bash needed)
README.md  CLAUDE.md            docs updated
```

## 1. Rebuild the firmware — required

`dex.h`, `moves.h` and `noart.h` compile into the ESP32 firmware. The
`web/firmware/*.bin` shipped in the tree predates this change, so:

```bash
bash tools/build_web.sh    # recompile + repack, then flash from web/index.html
# or a Python alternative (intended for Windows/MSYS2, no bash needed):
python tools/build_web.py   # compile, bump version, write the 4 parts
                            # + the merged tamapoke.bin; region paks untouched
```

(If your `build_web.sh` regenerates any of dex.h/moves.h/noart.h from
`tools/dex_data.py`, don't let it — the new tables come from
`tools/galar_paldea/gen_dex.py`. `index.html` itself needed no JS changes
beyond `REGION_PAKS`.)

## 2. Load the sprite packs

Flash, then use the web installer's "Load sprites" flow — it now downloads
`web/sprites-galar.pak` and `web/sprites-paldea.pak` along with the older
regions. Both new packs ship the same complete 1025-slot `thumbs.bin`
(1-809 copied from the Alola set + both new regions), so their install order
does not matter.

One pre-existing quirk worth knowing: `/mons/thumbs.bin` on the card is a
single file that each pack overwrites. If you install an OLD region pack
(Kanto…Alola) after a new one, its 809-slot thumbs file replaces the 1025-slot
one and the Galar/Paldea thumbnails disappear until you reinstall any new
pack. Sprites themselves are per-species files and are unaffected.

## 3. Saves

- `SAVE_VERSION` is now 2. Old (v1) saves load fine, migrated once on boot:
  the saved region index (`ALL` was 7, now 9 — Galar owns 7, Paldea 8) and
  the per-region egg cache (`eggR`, widened 8→10 slots with the old ALL entry
  moved to the new slot) are both remapped and persisted immediately, keyed
  by a `regv` NVS marker so it happens exactly once. v1 backup exports also
  restore, with the same remap. The migration is exercised by a host-side
  test (`tools/galar_paldea/save_test.cpp`) that compiles the real
  `save.cpp` against a mock Preferences.
- The Hisui gap 899-905 is deliberately excluded (per the requested ranges):
  those slots are "?" placeholders that never hatch and never appear wild.
  Pokédex totals show 1018 reachable species.
- 23 of the new species have no sprite art upstream (5 Galar, 18 Paldea) —
  42 no-art species total now. Per the project's existing rule they keep
  their dex numbers but never hatch and never appear in the wild hunt.

## Gameplay notes

- Wild hunt: the pool is the same two hatchable rarity tiers as before
  (R_COMUN and R_RARO base forms with art - evolution-only and legendary
  species never appear wild, by the existing design). Galar/Paldea species
  in those tiers join the pool once their pak is on the card.
- Cross-gen evolution links (base forms that really do evolve in the games):
  PRIMEAPE→979 (ANNIHILAPE), GIRAFARIG→981 (FARIGIRAF),
  DUNSPARCE→982 (DUDUNSPARCE), BISHARP→983 (KINGAMBIT).
- SIRFETCH'D (865), MR. RIME (866), CURSOLA (864) and OBSTAGOON (862) only
  evolve from GALARIAN forms, which this game does not model - their base
  forms keep evolvesTo 0 and the four are standalone R_RARO hatchables, the
  same rule already used for PERRSERKER, CLODSIRE and RUNERIGUS (branch
  conflicts MEOWTH/WOOPER/YAMASK also keep their old targets).
- Gym ladders still cover Kanto…Alola (`GYM_REGIONS` is 7): Galar and Paldea
  have starters, eggs, wild hunts and dex pages, but no gym ladder. The
  region pickers already cap at GYM_REGIONS, so nothing indexes wrong.
