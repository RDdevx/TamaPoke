// Wild encounters and catching, ported from the ShadowEnemyx fork onto this
// engine -- driven end to end through the real firmware: the prompt on the
// main screen, the fight on the real battle engine, the catch into the real
// party, the save through the real NVS. Plus the fix this build ships: the
// second creature out of a gym leader comes from the region you chose, not
// always Kanto's.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "trainers.h"
#include "battle.h"
#include "moves.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=29; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void FakeESP::restart(){exit(0);}

void setup(); void render(); void onTap(int16_t x, int16_t y);
void battleTap(int16_t x, int16_t y);
void startWildBattle(int16_t dex, uint8_t lvl, bool shiny);
void maybeOfferWildEncounter(uint32_t now);
void startTrainerBattle(uint8_t idx, bool hard);
void btlResolve(uint8_t yourMove);
extern Pet pet; extern Party party;
extern Combatant btlYou, btlFoe;
extern bool btlWild, battleOpen, btlOver, btlWon, btlLink;
extern uint8_t btlMsgCount, btlRegion, gymRegion, btlFoeAt;
extern int8_t btlTrainer, btlSwapWho;
extern bool btlCatchOffered, btlCatchTried, btlCatchDone, btlCatchSuccess;
extern bool btlCatchRespect;
extern uint8_t btlCatchChance;
extern uint8_t btlWildIv[4];
extern uint32_t wildPromptUntil, nextWildEligible, lastWildCheck;
extern int16_t wildPromptDex;
extern uint8_t wildPromptLevel;
extern bool wildPromptShiny;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// the wild prompt's buttons (drawWildPrompt): FIGHT on top, LATER below
#define WILD_FIGHT_Y 248
#define WILD_LATER_Y 300
// the catch decision reuses the move grid's top row: CATCH cell 0, SKIP cell 1
#define WILD_CATCH_X 149
#define WILD_SKIP_X  317
#define WILD_BTN_Y   296

// our strongest move id, straight off the combatant the engine built
static uint8_t damagingMove() {
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (btlYou.moves[i] && MOVE_TBL[btlYou.moves[i]].power) return btlYou.moves[i];
  return 0;
}

// resolve turns until the fight is over (clearing narration between them, as
// a player's taps would)
static bool fightToEnd() {
  for (int guard = 0; guard < 300 && !btlOver; guard++) {
    btlMsgCount = 0;
    btlResolve(damagingMove());
  }
  return btlOver;
}

// taps until the battle screen is gone: dismiss the narration, answer the
// catch offer (SKIP -- declining is decisive and closes the fight)
static void tapOut() {
  for (int i = 0; i < 8 && battleOpen; i++) {
    if (btlMsgCount) { battleTap(233, 100); continue; }
    if (btlOver && btlCatchOffered && !btlCatchDone) {
      battleTap(WILD_SKIP_X, WILD_BTN_Y);
      continue;
    }
    battleTap(233, 100);
  }
}

// the emulator's random(): one LCG step, high bits, mod n. The catch roll is
// the FIRST random call after the seed is set, so a seed can be picked in
// advance to land or miss the ball without side effects
static uint8_t roll100(uint32_t seed) {
  return (uint8_t)(((seed * 1103515245u + 12345u) >> 16) % 100);
}

// the catch state a fresh end-of-fight leaves, so a seeded retry is honest
static void rearmCatch() {
  btlCatchTried = btlCatchDone = btlCatchSuccess = false;
  btlCatchOffered = true;
  btlCatchRespect = false;
  btlCatchChance = 55;
  btlMsgCount = 0;
}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 50UL*MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  render();

  // --- the prompt: it appears on its own, on the main screen, and it can be
  // sent away. The first call only arms the cooldown.
  ck(wildPromptUntil == 0, "no prompt before any check has run");
  maybeOfferWildEncounter(millis());
  ck(wildPromptUntil == 0 && nextWildEligible > millis(),
     "the first check arms the cooldown and offers nothing");
  for (int i = 0; i < 800 && !wildPromptUntil; i++) {
    nextWildEligible = 1;   // the 20-minute cooldown: elapsed
    lastWildCheck = millis() - 60001;   // and the 1-minute gate: passed
    maybeOfferWildEncounter(millis());
  }
  ck(wildPromptUntil != 0, "a wild creature appears on the main screen");
  ck(wildPromptDex >= 1 && wildPromptDex <= DEX_COUNT && wildPromptLevel >= 1,
     "it is a real species at a real level");
  ck(wildPromptShiny == false, "shiny stays rare (this roll was not one)");
  uint32_t now = millis();
  ck(nextWildEligible > now, "offering it re-arms the cooldown");

  // LATER sends it away, and the cooldown holds the next one off
  onTap(233, WILD_LATER_Y);
  ck(wildPromptUntil == 0, "LATER sends the creature away");
  for (int i = 0; i < 50; i++) {
    lastWildCheck = 0;
    maybeOfferWildEncounter(millis());
  }
  ck(wildPromptUntil == 0, "the cooldown holds the prompt off");

  // --- the fight: exactly the creature that appeared, on the real engine
  startWildBattle(16, 3, true);   // a shiny PIDGEY, far below us
  ck(battleOpen && btlWild && btlTrainer < 0, "the wild fight opens");
  ck(btlFoe.dex == 16 && btlFoe.level == 3 && btlFoe.shiny,
     "against exactly the creature that appeared");
  ck(btlWildIv[0] >= 8 && btlWildIv[0] < 32 && btlWildIv[3] >= 8 && btlWildIv[3] < 32,
     "the wild creature rolled its own IVs");
  ck(!btlCatchOffered, "no catch offer mid-fight");

  // a win always offers the ball
  ck(fightToEnd(), "the fight ends");
  ck(btlWon, "a level-51 squad beats a level-3 wild");
  ck(btlCatchOffered && !btlCatchRespect, "a win offers the catch, at the full chance");
  ck(btlCatchChance >= 10 && btlCatchChance <= 75, "the chance is the sane range");
  battleTap(233, 100);            // dismiss the win line
  ck(battleOpen, "the fight stays open for the catch decision");
  battleTap(WILD_SKIP_X, WILD_BTN_Y);
  ck(btlCatchDone && !btlCatchTried, "SKIP declines without a roll");
  ck(party.count() == 0, "nothing was caught");
  tapOut();
  ck(!battleOpen, "the fight closes after the decision");

  // --- a close LOSS still offers the ball, at the reduced "respect" chance
  startWildBattle(16, 3, false);
  btlFoe.hp = btlFoe.maxHp / 4;   // visibly battered...
  btlYou.hp = 0;                  // ...but we went down
  btlMsgCount = 0;
  btlResolve(0);                   // move 0: we do not act, the fight just ends
  ck(btlOver && !btlWon, "the wild creature can win");
  ck(btlCatchOffered && btlCatchRespect,
     "a close loss still offers -- at the respect chance");
  ck(btlCatchChance >= 5 && btlCatchChance <= 25,
     "and the respect chance is the reduced one");
  tapOut();
  ck(!battleOpen, "that fight closes too");

  // --- CATCH lands: the creature joins with exactly what it fought with.
  // The seed is picked against the odds the fight actually left us with, so
  // the ball lands on purpose and the party only ever gains one member.
  startWildBattle(16, 3, false);
  fightToEnd();
  battleTap(233, 100);             // dismiss the win line
  bool closeWin = btlYou.hp * 3 <= btlYou.maxHp;
  uint8_t chance = pet.catchChanceForWild(16, 3, btlYou.level, closeWin);
  uint32_t okSeed = 0, missSeed = 0;
  for (uint32_t s = 1; s <= 100000 && !(okSeed && missSeed); s++) {
    uint8_t roll = roll100(s);
    if (!okSeed && roll < chance) okSeed = s;
    if (!missSeed && roll >= chance) missSeed = s;
  }
  ck(okSeed && missSeed, "both a landing and a missing roll exist");
  rearmCatch();
  g_seed = okSeed;
  battleTap(WILD_CATCH_X, WILD_BTN_Y);
  ck(btlCatchSuccess, "CATCH lands at the full chance");
  ck(btlCatchTried && btlCatchDone, "and it is a one-shot decision");
  ck(party.count() == 1, "the creature joins the party");
  {
    const PartyMon &m = party.slots[0];
    ck(m.dex == 16 && m.level == 3, "same species and level");
    ck(m.ivAtk == btlWildIv[0] && m.ivDef == btlWildIv[1] &&
       m.ivSpe == btlWildIv[2] && m.ivHp == btlWildIv[3],
       "it keeps the IVs it fought with");
    ck(memcmp(m.moves, btlFoe.moves, MOVE_SLOTS) == 0,
       "and the moves it fought with");
  }
  ck(pet.isCaught(16), "the Pokedex marks it caught");
  ck(!pet.hasBadge(gymRegion, 0, false), "a wild win awards no badge");
  {
    Pet again; again.begin();
    ck(again.isCaught(16), "the catch is saved");
  }
  tapOut();
  ck(!battleOpen, "the fight closes after a successful catch");

  // --- and the ball can miss, catching nothing
  startWildBattle(16, 3, false);
  fightToEnd();
  battleTap(233, 100);
  rearmCatch();
  g_seed = missSeed;
  battleTap(WILD_CATCH_X, WILD_BTN_Y);
  ck(btlCatchDone && !btlCatchSuccess, "the ball can miss");
  ck(party.count() == 1, "a miss catches nothing");
  tapOut();

  // --- a catch with the party full lands in the box -- and SAVES there
  // (party.save() does not write the box; the box has its own key)
  while (party.firstFree() >= 0 && party.count() < PARTY_SLOTS) {
    PartyMon m; m.dex = 1; m.level = 5;
    party.add(m);
  }
  startWildBattle(10, 3, false);      // CATERPIE, so the catch is unambiguous
  fightToEnd();
  battleTap(233, 100);
  closeWin = btlYou.hp * 3 <= btlYou.maxHp;
  chance = pet.catchChanceForWild(10, 3, btlYou.level, closeWin);
  okSeed = 0;
  for (uint32_t s = 1; s <= 100000 && !okSeed; s++)
    if (roll100(s) < chance) okSeed = s;
  rearmCatch();
  g_seed = okSeed;
  battleTap(WILD_CATCH_X, WILD_BTN_Y);
  ck(btlCatchSuccess, "the catch still lands with a full party");
  ck(party.count() == PARTY_SLOTS, "the party stays full");
  ck(party.boxCount() == 1, "and the creature goes to the box");
  {
    const PartyMon &m = party.box[0];
    ck(m.dex == 10 && m.level == 3, "the boxed creature, same species and level");
    ck(m.ivAtk == btlWildIv[0] && m.ivDef == btlWildIv[1] &&
       m.ivSpe == btlWildIv[2] && m.ivHp == btlWildIv[3],
       "with the IVs it fought with");
    ck(memcmp(m.moves, btlFoe.moves, MOVE_SLOTS) == 0, "and its moveset");
  }
  {
    Pet again; again.begin();
    ck(again.isCaught(10), "the boxed catch registers in the Pokedex");
    Party p2; p2.begin();
    ck(p2.boxCount() == 1 && p2.box[0].dex == 10,
       "and it is still in the box after a reload");
  }
  tapOut();

  // --- no room anywhere: no offer at all
  while (party.firstFree() >= 0) {
    PartyMon m; m.dex = 1; m.level = 5;
    party.add(m);
  }
  while (party.boxFirstFree() >= 0) {
    PartyMon m; m.dex = 1; m.level = 5;
    party.boxAdd(m);
  }
  startWildBattle(16, 3, false);
  ck(fightToEnd() && btlWon, "the fight still happens with a full party");
  ck(!btlCatchOffered, "a full party and box means no catch offer");
  tapOut();

  // --- THE GYM FIX: the second creature out of a leader comes from the region
  // you chose. Before the fix btlRegion was never latched, so it was always
  // Kanto's BROCK -- every region's second creature was ONIX.
  gymRegion = 1;                     // JOHTO
  btlRegion = 0;                     // the stale value the bug left behind
  startTrainerBattle(0, false);      // FALKNER: PIDGEY L7, PIDGEOTTO L9
  ck(btlRegion == 1, "the fight latches the region it is in");
  ck(btlTrainer == 0 && !btlWild, "a gym fight, not a wild one");
  btlFoe.hp = 1;
  btlMsgCount = 0;
  btlResolve(damagingMove());        // knock out the PIDGEY
  ck(btlSwapWho == 1, "the leader has another creature ready");
  battleTap(233, 100);               // dismiss the line: the swap happens here
  ck(btlFoe.dex == 17 && btlFoe.level == 9,
     "the second creature is Falkner's PIDGEOTTO");
  ck(TRAINER_SETS[0].list[0].team[1].dex == 95,
     "(Kanto's second is ONIX, so this is what the bug used to do)");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
