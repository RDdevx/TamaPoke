// Growing the box must not lose what is already in it, and a full box must
// survive a backup and restore. Uses the real party.cpp and save.cpp against an
// in-memory NVS stand-in (tests/stub/Preferences.h).
#include "../party.h"
#include "../save.h"
#include "../dex.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static PartyMon mon(int i) {            // a recognisable creature per slot
  PartyMon m;
  m.dex = (int16_t)(1 + i % DEX_COUNT);
  m.level = (uint16_t)(10 + i);
  m.medals = (uint16_t)i;
  m.ivAtk = (uint8_t)(i % 32); m.ivHp = 31;
  m.trAtk = (uint8_t)(i % 70);
  snprintf(m.nick, sizeof(m.nick), "N%d", i);
  m.moves[0] = 1;
  return m;
}
static bool same(const PartyMon &a, const PartyMon &b) { return memcmp(&a, &b, sizeof(PartyMon)) == 0; }

static void storeBox(int records) {     // what an older build left in NVS
  std::vector<PartyMon> v;
  for (int i = 0; i < records; i++) v.push_back(mon(i));
  Preferences p; p.begin("tamapoke", false);
  p.putBytes("box", v.data(), v.size() * sizeof(PartyMon));
  p.end();
}
static void wipe() { Preferences p; p.begin("tamapoke", false); p.clear(); p.end(); }

int main() {
  // 1. THE reported risk: an 18-slot box written by the old build, read by this one.
  wipe(); storeBox(18);
  {
    Party p; p.begin();
    int ok = 0;
    for (int i = 0; i < 18; i++) ok += same(p.box[i], mon(i));
    CHECK(ok == 18, "all 18 existing creatures survive the upgrade, got %d", ok);
    CHECK(p.boxCount() == 18, "box count is 18, got %d", p.boxCount());
    for (int i = 18; i < BOX_SLOTS; i++) CHECK(p.box[i].empty(), "new slot %d starts empty", i);
    // The save that follows must not shrink or lose them: deposit one and reload.
    CHECK(p.boxAdd(mon(500)), "can deposit into the new space");
    Party q; q.begin();
    int again = 0;
    for (int i = 0; i < 18; i++) again += same(q.box[i], mon(i));
    CHECK(again == 18, "still all 18 after the first save at the new size, got %d", again);
    CHECK(same(q.box[18], mon(500)), "the new deposit landed right after them");
    Preferences pr; pr.begin("tamapoke", true);
    CHECK(pr.getBytesLength("box") == sizeof(PartyMon) * BOX_SLOTS, "box is now saved at the full size");
    pr.end();
  }
  // 2. Any smaller multiple loads too (a build between 18 and 96, or a hand-edited save).
  for (int n : { 1, 6, 24, 60, 95 }) {
    wipe(); storeBox(n);
    Party p; p.begin();
    int ok = 0; for (int i = 0; i < n; i++) ok += same(p.box[i], mon(i));
    CHECK(ok == n && p.boxCount() == n, "%d-record box migrates (got %d / %d)", n, ok, p.boxCount());
  }
  // 3. A blob that is not whole records is junk: ignored, never misread.
  wipe(); { Preferences p; p.begin("tamapoke", false); uint8_t junk[541]; memset(junk, 7, sizeof(junk)); p.putBytes("box", junk, sizeof(junk)); p.end(); }
  { Party p; p.begin(); CHECK(p.boxCount() == 0, "a ragged blob loads as an empty box, not garbage (%d)", p.boxCount()); }
  // 4. A bigger box than this build has keeps the first BOX_SLOTS (unchanged behaviour).
  wipe(); storeBox(BOX_SLOTS + 24);
  { Party p; p.begin(); int ok = 0; for (int i = 0; i < BOX_SLOTS; i++) ok += same(p.box[i], mon(i));
    CHECK(ok == BOX_SLOTS, "an oversized box keeps the first %d", BOX_SLOTS); }

  // 5. A full box goes through EXPORT and IMPORT, and back out the other side.
  wipe(); storeBox(BOX_SLOTS);
  {
    uint8_t *buf = (uint8_t *)malloc(SAVE_BUF_MAX);
    size_t need = saveExportSize();
    CHECK(need <= SAVE_BUF_MAX, "saveExportSize %zu fits SAVE_BUF_MAX %d", need, SAVE_BUF_MAX);
    size_t n = saveExport(buf, SAVE_BUF_MAX);
    CHECK(n > 0, "a full box can be exported at all");
    CHECK(n <= need, "export (%zu) is within what saveExportSize promised (%zu)", n, need);
    CHECK(n > sizeof(PartyMon) * BOX_SLOTS, "the box is actually IN the backup (%zu bytes)", n);
    wipe();
    CHECK(saveImport(buf, n), "the backup restores");
    Party p; p.begin();
    int ok = 0; for (int i = 0; i < BOX_SLOTS; i++) ok += same(p.box[i], mon(i));
    CHECK(ok == BOX_SLOTS, "all %d creatures come back from the backup, got %d", BOX_SLOTS, ok);
    free(buf);
  }

  printf(failures ? "\n%d FAILURES\n" : "\nbox migration test passed\n", failures);
  return failures ? 1 : 0;
}
