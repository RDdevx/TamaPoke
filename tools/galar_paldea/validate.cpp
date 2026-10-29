// Validation harness for the Galar/Paldea expansion. Compiles the generated
// data headers with plain g++ (no Arduino needed) and checks the documented
// invariants. Mirrors the checks tools/emu/tests would run.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "dex.h"
#include "moves.h"
#include "noart.h"

static int fails = 0;
#define CHECK(cond, ...) do { \
  if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } \
} while (0)

int main() {
  printf("DEX_COUNT=%d REGION_COUNT=%d REGION_ALL=%d DEX_REACHABLE=%d\n",
         DEX_COUNT, REGION_COUNT, REGION_ALL, DEX_REACHABLE);

  // --- table sizes ---
  CHECK(DEX_COUNT == 1025, "dex count");
  CHECK(REGION_COUNT == 10, "region count");
  CHECK(REGION_ALL == 9, "region all");
  CHECK(dexSlotPlayable(810) && dexSlotPlayable(1025), "galar/paldea playable");
  CHECK(!dexSlotPlayable(899) && !dexSlotPlayable(905), "hisui unreachable");
  CHECK(dexSlotPlayable(898) && dexSlotPlayable(906), "hisui bounds");

  // --- regions tile the dex ---
  struct { int lo, hi; } want[] = {
    {1,151},{152,251},{252,386},{387,493},{494,649},{650,721},{722,809},
    {810,898},{906,1025}
  };
  for (int r = 0; r < 9; r++) {
    CHECK(REGIONS[r].lo == want[r].lo && REGIONS[r].hi == want[r].hi,
          "region %d range %d-%d", r, REGIONS[r].lo, REGIONS[r].hi);
    CHECK(REGIONS[r].starterCount >= 3, "region %d starters", r);
  }
  CHECK(strcmp(REGIONS[7].name, "GALAR") == 0, "galar name");
  CHECK(strcmp(REGIONS[8].name, "PALDEA") == 0, "paldea name");
  CHECK(REGIONS[9].hi == 1025 && REGIONS[9].starterCount == 29, "all region");
  int galarStart[3] = {810, 813, 816};
  for (int i = 0; i < 3; i++)
    CHECK(REGIONS[7].starters[i] == galarStart[i], "galar starter %d", i);
  int paldeaStart[3] = {906, 909, 912};
  for (int i = 0; i < 3; i++)
    CHECK(REGIONS[8].starters[i] == paldeaStart[i], "paldea starter %d", i);

  // --- every species row is sane ---
  int evoTargets = 0, raro = 0, comun = 0, leg = 0, evo = 0;
  for (int d = 1; d <= DEX_COUNT; d++) {
    const DexEntry &e = DEX_TBL[d];
    CHECK(e.name && e.name[0], "name %d", d);
    if (!dexSlotPlayable(d)) continue;
    CHECK(e.type1 <= 17, "type1 %d", d);
    CHECK(e.rarity <= R_LEGENDARIO, "rarity %d", d);
    if (e.evolvesTo) {
      CHECK(e.evolvesTo >= 1 && e.evolvesTo <= DEX_COUNT, "evolvesTo %d", d);
      CHECK(e.evolvesTo < 899 || e.evolvesTo > 905, "evolvesTo hisui %d", d);
      uint8_t tr = DEX_TBL[e.evolvesTo].rarity;
      CHECK(tr == R_EVO || tr == R_LEGENDARIO, "target rarity %d->%d", d, e.evolvesTo);
    }
    if (e.rarity == R_EVO) evo++;
    if (e.rarity == R_COMUN) comun++;
    if (e.rarity == R_RARO) raro++;
    if (e.rarity == R_LEGENDARIO) leg++;
  }
  printf("rarity mix: evo=%d comun=%d raro=%d leg=%d\n", evo, comun, raro, leg);

  // --- documented cross-gen links ---
  // PRIMEAPE, GIRAFARIG, DUNSPARCE and BISHARP really do evolve into their
  // Gen 9 targets as base forms. FARFETCH'D, MR. MIME, CORSOLA and LINOONE
  // only evolve as GALARIAN forms, which this game does not model, so they
  // keep evolvesTo 0 and the targets are standalone R_RARO hatchables
  // (the PERRSERKER rule).
  struct { int d, t; } links[] = {
    {57, 979}, {203, 981}, {206, 982}, {625, 983},
  };
  for (auto &l : links)
    CHECK(DEX_TBL[l.d].evolvesTo == l.t, "link %d->%d got %d",
          l.d, l.t, DEX_TBL[l.d].evolvesTo);
  int formGated[] = {83, 122, 222, 264};
  int formTargets[] = {865, 866, 864, 862};
  for (int i = 0; i < 4; i++) {
    CHECK(DEX_TBL[formGated[i]].evolvesTo == 0, "form-gated %d linked",
          formGated[i]);
    CHECK(DEX_TBL[formTargets[i]].rarity == R_RARO, "standalone %d not RARO",
          formTargets[i]);
  }
  int standaloneAll[] = {842, 862, 863, 864, 865, 866, 867, 937, 980, 1011};
  for (int t : standaloneAll)
    CHECK(DEX_TBL[t].rarity == R_RARO, "standalone %d not RARO", t);
  CHECK(DEX_TBL[52].evolvesTo == 53, "meowth keeps persian");
  CHECK(DEX_TBL[194].evolvesTo == 195, "wooper keeps quagsire");
  CHECK(DEX_TBL[562].evolvesTo == 563, "yamask keeps cofagrigus");

  // --- R_EVO must be somebody's table target (or the documented exceptions) ---
  bool target[DEX_COUNT + 1] = {};
  for (int d = 1; d <= DEX_COUNT; d++)
    if (DEX_TBL[d].evolvesTo) target[DEX_TBL[d].evolvesTo] = true;
  for (int d = 1; d <= DEX_COUNT; d++) {
    if (!dexSlotPlayable(d)) continue;
    if (DEX_TBL[d].rarity == R_EVO) {
      bool eeveeBranch = false;
      for (uint8_t i = 0; i < EEVEE_EVO_COUNT; i++)
        if (EEVEE_EVOS[i] == d) eeveeBranch = true;
      CHECK(target[d] || eeveeBranch, "orphaned R_EVO %d (%s)", d, DEX_TBL[d].name);
    }
  }

  // --- starters and probes must have art ---
  for (int d : {810, 813, 816, 906, 909, 912})
    CHECK(speciesHasArt(d), "starter art %d", d);
  // sdRegionProbe: first/middle/last packed species of the region
  // sdRegionProbe() inlined (sdmon.h pulls in SD_MMC/FS.h):
  auto sdRegionProbe = [](uint8_t r, uint8_t i) -> int16_t {
    const RegionInfo &rg = REGIONS[r % REGION_COUNT];
    int16_t want = (i == 0) ? rg.lo : (i == 1) ? (int16_t)((rg.lo + rg.hi) / 2) : rg.hi;
    for (int16_t d = want; d <= rg.hi; d++) if (speciesHasArt(d)) return d;
    for (int16_t d = want; d >= rg.lo; d--) if (speciesHasArt(d)) return d;
    return want;
  };
  for (uint8_t r = 0; r < REGION_COUNT; r++) {
    if (r == REGION_ALL) continue;
    for (uint8_t i = 0; i < 3; i++) {
      int16_t p = sdRegionProbe(r, i);
      CHECK(p >= REGIONS[r].lo && p <= REGIONS[r].hi, "probe %d.%d = %d", r, i, p);
      CHECK(speciesHasArt(p), "probe art %d", p);
    }
  }

  // --- learnsets ---
  printf("learn entries: %d\n", (int)(LEARN_OFS[DEX_COUNT + 1]));
  for (int d = 1; d <= DEX_COUNT; d++) {
    uint8_t n = learnCount(d);
    for (uint8_t i = 0; i < n; i++) {
      uint8_t mv = learnMove(d, i);
      CHECK(mv < MOVE_COUNT, "learn move %d[%d]=%d", d, i, mv);
      uint8_t lv = learnLevel(d, i);
      CHECK(lv <= 100, "learn level %d[%d]=%d", d, i, lv);
    }
  }
  // hisui placeholders have empty learnsets (no-art mons keep theirs, as in
  // the existing table: no-art does not mean no data)
  for (int d = 899; d <= 905; d++)
    CHECK(learnCount(d) == 0, "hisui learnset %d", d);
  // learnsets must be ascending by level within the level-up tail (pet.cpp
  // breaks its scan on the first level above the pet's)
  for (int d = 810; d <= 1025; d++) {
    if (d >= 899 && d <= 905) continue;
    uint8_t n = learnCount(d);
    int last = 0;
    for (uint8_t i = 0; i < n; i++) {
      uint8_t lv = learnLevel(d, i);
      if (lv > 0) CHECK(lv >= last, "unsorted learnset %d[%d]", d, i);
      if (lv > 0) last = lv;
    }
  }

  // --- new species spot checks ---
  CHECK(strcmp(DEX_TBL[810].name, "GROOKEY") == 0, "grookey name %s", DEX_TBL[810].name);
  CHECK(strcmp(DEX_TBL[866].name, "MR. RIME") == 0, "mr rime %s", DEX_TBL[866].name);
  CHECK(strcmp(DEX_TBL[925].name, "MAUSHOLD") == 0, "maushold %s", DEX_TBL[925].name);
  CHECK(strcmp(DEX_TBL[1025].name, "PECHARUNT") == 0, "pecharunt %s", DEX_TBL[1025].name);
  CHECK(DEX_TBL[810].evolvesTo == 811 && DEX_TBL[810].evolveLevel == 16, "grookey evo");
  CHECK(DEX_TBL[888].rarity == R_LEGENDARIO && DEX_TBL[890].rarity == R_LEGENDARIO,
        "zacian/eternatus legendary");
  CHECK(DEX_TBL[1024].rarity == R_LEGENDARIO, "terapagos legendary");
  CHECK(DEX_TBL[810].rarity == R_RARO, "grookey raro");

  if (fails) { printf("%d FAILURES\n", fails); return 1; }
  printf("ALL CHECKS PASSED\n");
  return 0;
}
