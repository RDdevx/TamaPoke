// Who comes in when a squad member faints. Both sides used to take "the next
// index", which ended a fight with live teammates behind the fainted one and could
// send in a creature that was already down.
#include "../battle.h"
#include <cstdio>
#include <stdint.h>
void sfxPlay(uint8_t) {}
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void squad(Combatant *s, const char *hp) {   // 'A' alive, 'x' fainted
  for (int i = 0; hp[i]; i++) { s[i].dex = 1 + i; s[i].maxHp = 10; s[i].hp = hp[i] == 'x' ? 0 : 10; }
}

int main() {
  Combatant s[6];
  // The reported bug: you chose the LAST one, it faints, others are still alive.
  squad(s, "AAx");  CHECK(nextStanding(s, 3, 2) == 0, "last faints, first alive -> 0, got %d", nextStanding(s, 3, 2));
  squad(s, "xAx");  CHECK(nextStanding(s, 3, 2) == 1, "last faints, middle alive -> 1");
  // Normal order still goes forward first.
  squad(s, "xAA");  CHECK(nextStanding(s, 3, 0) == 1, "first faints -> next in order");
  squad(s, "AAA");  CHECK(nextStanding(s, 3, 1) == 2, "forward before wrapping");
  // The other half: never send in one that is already down.
  squad(s, "xxA");  CHECK(nextStanding(s, 3, 0) == 2, "skips a fainted next member, got %d", nextStanding(s, 3, 0));
  squad(s, "Axx");  CHECK(nextStanding(s, 3, 1) == 0, "skips fainted ahead, wraps to the live one");
  // Only an empty field ends the fight.
  squad(s, "xxx");  CHECK(nextStanding(s, 3, 1) == -1, "everyone down -> -1");
  squad(s, "x");    CHECK(nextStanding(s, 1, 0) == -1, "a squad of one -> -1");
  CHECK(nextStanding(s, 0, 0) == -1, "empty squad -> -1");
  // Never returns the current slot, even if its record is stale and says alive
  // (the active creature is written back only when it leaves).
  squad(s, "A");    CHECK(nextStanding(s, 1, 0) == -1, "never returns itself");
  squad(s, "AAAAAA"); s[3].hp = 0; CHECK(nextStanding(s, 6, 3) == 4, "six-member squad");
  squad(s, "AAAAAA"); for (int i = 0; i < 5; i++) s[i].hp = 0; CHECK(nextStanding(s, 6, 4) == 5, "only the last is left");
  // Whole fight: faint them one at a time in an awkward order; must visit everyone once.
  squad(s, "AAAA");
  int at = 3, seen = 0;                       // player had switched to the last one first
  for (;;) { s[at].hp = 0; seen++; int n = nextStanding(s, 4, at); if (n < 0) break; CHECK(!s[n].fainted(), "sent in a fainted one"); at = n; }
  CHECK(seen == 4, "every creature fights before the battle ends (%d of 4)", seen);
  printf(failures ? "\n%d FAILURES\n" : "\nbattle order test passed\n", failures);
  return failures;
}
