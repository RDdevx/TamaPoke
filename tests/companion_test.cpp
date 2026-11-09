// A revived creature must be an ORDINARY pet: it ages, levels, evolves and
// learns moves. It used to be a frozen "companion" that did none of those.
#include "../pet.h"
#include <cstdio>
#include <stdint.h>
void sfxPlay(uint8_t) {}
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int known(const Pet &p) { int n = 0; for (int i = 0; i < MOVE_SLOTS; i++) if (p.moves[i]) n++; return n; }

int main() {
  // --- ages and levels ---
  {
    Pet p;
    PartyMon m; m.dex = 25; m.level = 30;
    p.reviveFrom(m);
    CHECK(p.level() == 30, "revived at its banked level, got %u", p.level());
    CHECK(!p.frozen, "no longer a frozen companion");
    for (int i = 0; i < MINUTES_PER_LEVEL * 5; i++) p.update(i * 60000UL + 60000UL);
    CHECK(p.level() >= 34, "five hours later it must have levelled, level %u", p.level());
  }

  // --- evolves: a species that is still a basic form, revived below its level ---
  {
    Pet p;
    PartyMon m; m.dex = 4; m.level = 5;     // Charmander
    p.reviveFrom(m);
    uint8_t need = DEX_TBL[4].evolveLevel;
    CHECK(need > 5, "test premise: Charmander evolves above level 5 (needs %u)", need);
    CHECK(!p.canEvolveNow(), "not yet at level 5");
    // age it to the evolve level, keeping it cared for so lowestStat() >= 40
    for (int i = 0; i < MINUTES_PER_LEVEL * (need + 2); i++) {
      p.update(i * 60000UL + 60000UL);
      p.fullness = p.joy = p.energy = p.hygiene = 100;
      p.sleeping = false;
    }
    CHECK(p.level() >= need, "reached the evolve level, level %u need %u", p.level(), need);
    CHECK(p.canEvolveNow(), "a revived creature must be able to evolve");
    p.evolve();
    CHECK(p.speciesId == DEX_TBL[4].evolvesTo, "evolved into %d", (int)p.speciesId);
  }

  // --- learns moves as it levels ---
  {
    Pet p;
    PartyMon m; m.dex = 4; m.level = 3;
    m.moves[0] = 1;                          // one move known, three free slots
    p.reviveFrom(m);
    int before = known(p) + p.learnQCount;
    for (int i = 0; i < MINUTES_PER_LEVEL * 40; i++) {
      p.update(i * 60000UL + 60000UL);
      p.fullness = p.joy = p.energy = p.hygiene = 100;
      p.sleeping = false;
    }
    CHECK(known(p) + p.learnQCount > before, "it must learn new moves while levelling (had %d, now %d)",
          before, known(p) + p.learnQCount);
    CHECK(p.lastLearnLevel == p.level(), "learn gates keep up with the level");
  }

  printf(failures ? "\n%d FAILURES\n" : "\ncompanion test passed\n", failures);
  return failures;
}
