# Galar + Paldea pipeline

Everything that added regions 8 (Galar, dex 810–898) and 9 (Paldea, dex
906–1025) to the tree: starters, full regional dexes, wild-hunt support and the
`sprites-galar.pak` / `sprites-paldea.pak` web bundles. Run from this folder.

## Order of operations

```bash
# 1. species data from PokeAPI -> cache/pokedata.json
python3 fetch_pokeapi.py

# 2. sprites from SpriteCollab -> out/sprites-galar.pak, out/sprites-paldea.pak
#    (NOT idempotent-safe to skip: chain the builds with --thumbs-from so BOTH
#    paks ship the same complete 1025-slot thumbs.bin - /mons/thumbs.bin is a
#    single overwritten file on the card, so every pak must carry every
#    region's thumbnails. Copy the results into web/ afterwards.)
python3 spritepack.py --range 810-898 --name galar
python3 spritepack.py --range 906-1025 --name paldea --thumbs-from out/sprites-galar.pak
python3 spritepack.py --range 810-898 --name galar --thumbs-from out/sprites-paldea.pak
cp out/sprites-galar.pak out/sprites-paldea.pak ../../web/

# 3. regenerate dex.h / moves.h / noart.h in the project root.
#    ONE-TIME conversion: it expects the PRE-Galar/Paldea headers (it asserts
#    the old 810-row DEX_TBL). Keep a pristine copy of the original headers
#    if you ever need to re-run it.
python3 gen_dex.py cache/pokedata.json ../.. noart.json

# 4. invariant harness (desktop g++; compiles the generated headers - it does
#    NOT read the paks; pak checks are in the session scripts)
g++ -std=c++17 -I../.. validate.cpp -o validate && ./validate

# 5. save-migration test: compiles the REAL save.cpp against a mock
#    Preferences (stub/) and exercises the v1->v2 import remap, v2 round-trips,
#    corrupt-blob rejection, and the Pet::load() region migration snippet.
g++ -std=c++17 -I stub -I ../.. save_test.cpp -o save_test && ./save_test
```

The sprite cache (`cache/sc/`) makes rebuilds fast; caches are kept out of the
repo. `gen_dex.py` and the fetch step are deterministic given the same cache.

## What each step produces

- `fetch_pokeapi.py` — base stats, types, evolution chains (PokeAPI species
  names are canonical; the `pokemon` endpoint has variant suffixes), and
  level-up + machine learnsets per species. `level_learned_at`, not `level`.
- `spritepack.py` — SpriteCollab `sprite/<dex>/` (default) and
  `sprite/<dex>/<form>/shiny/` converted to the project's TPK2/TPTH formats and
  bundled as TPAK (`mons/pNNN.bin`, `mons/psNNN.bin`, `mons/thumbs.bin`).
  Act ids: Idle 0, Walk 1 (raw, faces left), Walk 2 (mirrored), Sleep 3, Eat 4,
  Hurt 5, Attack 6, Pose 7, Hop 8, Nod 9, DeepBreath 10, Sit 11. Frame ms =
  `max(70, round(dur*70/3))` from SpriteCollab Durations.
- `gen_dex.py` — emits the new dex rows (810–1025), learnsets, and merges
  `noart.json` into `noart.h`. DexEntry row order:
  name, evolvesTo, evolveLevel, rarity, accent, bHp, bAtk, bDef, bSpe, bSpA,
  bSpD, biome, type1, type2 — note bSpe is the 4th stat field. Cross-gen links
  and the Hisui gap (899–905, kept as `?` placeholders reachable only via
  `dexSlotPlayable()`) are decided inside this script; see the printout.
- `validate.cpp` — the checks that gate a merge, over the generated HEADERS
  only (it does not read the paks): table sizes, region tiling, region probes
  land in-range on species with art, every species row sane, the documented
  cross-gen links + form-gated rules + standalone RARO set, learnset offsets
  and levels. Pak content was checked separately (thumbs coverage, TPAK layout)
  during the original build.
- `save_test.cpp` — compiles the REAL `save.cpp` against the mock Preferences
  in `stub/` and exercises the v1→v2 import remap, the eggR widening, dex
  bitmap zero-extension, v2 round-trips, corrupt-blob rejection, and the
  `Pet::load()` migration snippet (mirrored — keep in sync with pet.cpp).

## Firmware note

`dex.h`/`moves.h`/`noart.h` compile into the ESP32 firmware — after
regenerating them, rebuild with `bash tools/build_web.sh` and re-flash; the
`web/firmware/*.bin` shipped in the tree predates the new regions.
