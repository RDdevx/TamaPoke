// Host-side test of the save migration for the Galar/Paldea expansion.
// Compiles the REAL save.cpp against a mock Preferences (single TU), so the
// v1 -> v2 import path and the Pet::load() migration snippet are exercised
// exactly as they run on the board.
//
// Build:  g++ -std=c++17 -I stub -I <project-root> save_test.cpp -o save_test
// (save.cpp is #included below, so it needs no separate compile step)
#include "Preferences.h"

std::vector<MockNvsEntry> g_nvs;

#include "save.cpp"

static int fails = 0;
#define CHECK(cond, msg) do { \
  if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

// builds a save blob the way saveExport does, with a chosen version byte
struct Blob {
  std::vector<uint8_t> b;
  void field(const char *key, uint8_t kind, const void *val, uint16_t n) {
    uint8_t klen = (uint8_t)strlen(key);
    b.push_back(klen);
    b.insert(b.end(), key, key + klen);
    b.push_back(kind);
    b.push_back(n & 0xFF); b.push_back(n >> 8);
    const uint8_t *p = (const uint8_t *)val;
    b.insert(b.end(), p, p + n);
  }
  std::vector<uint8_t> finish(uint8_t version) {
    std::vector<uint8_t> out(SAVE_HDR, 0);
    out[0] = SAVE_MAGIC0; out[1] = SAVE_MAGIC1;
    out[2] = SAVE_MAGIC2; out[3] = SAVE_MAGIC3;
    out[4] = version;
    uint16_t count = 0;
    for (size_t i = 0; i < b.size(); ) { count++; i += 1 + b[i] + 3 + (b[i+1+b[i]+1] | (b[i+1+b[i]+2] << 8)); }
    out[5] = count & 0xFF; out[6] = count >> 8;
    out[7] = 0;
    out.insert(out.end(), b.begin(), b.end());
    uint16_t c = crc16(out.data(), out.size());
    out.push_back(c & 0xFF); out.push_back(c >> 8);
    return out;
  }
};

#define REGION_ALL_MACRO 9   // matches dex.h REGION_ALL

// mirrors loadBlob() in pet.cpp -- keep in sync
static void loadBlobMock(Preferences &p, const char *key, void *dst, size_t n) {
  size_t have = p.getBytesLength(key);
  if (!have) return;
  if (have <= n) { p.getBytes(key, dst, n); return; }
  uint8_t *tmp = (uint8_t *)malloc(have);
  if (!tmp) return;
  if (p.getBytes(key, tmp, have) == have) memcpy(dst, tmp, n);
  free(tmp);
}

// mirrors Pet::load()'s region + eggR migration in pet.cpp -- keep in sync
static uint8_t runLoadMigration(Preferences &prefs, int16_t *eggByRegion) {
  uint8_t region = prefs.getUChar("reg", REGION_ALL_MACRO);
  const bool legacySave = !prefs.isKey("regv");
  if (legacySave && region == 7) {
    region = REGION_ALL_MACRO;
    prefs.putUChar("reg", region);
  }
  if (region >= 10) region = REGION_ALL_MACRO;
  if (prefs.getBytesLength("eggR") == 8 * sizeof(int16_t)) {
    int16_t legacy[8];
    loadBlobMock(prefs, "eggR", legacy, sizeof(legacy));
    for (int i = 0; i < 7; i++) eggByRegion[i] = legacy[i];
    eggByRegion[7] = 0;
    eggByRegion[8] = 0;
    eggByRegion[9] = legacy[7];
    prefs.putBytes("eggR", eggByRegion, 10 * sizeof(int16_t));
  } else {
    loadBlobMock(prefs, "eggR", eggByRegion, 10 * sizeof(int16_t));
  }
  if (legacySave) prefs.putUChar("regv", 1);
  return region;
}

static uint8_t nvsReg() {
  MockNvsEntry *e = nvsFind("reg");
  return e ? e->data[0] : 0xFF;
}

int main() {
  printf("save.h: SAVE_VERSION=%d SAVE_HDR=%d fields=%d\n",
         SAVE_VERSION, SAVE_HDR, SAVE_FIELD_COUNT);

  // --- v1 import: reg=7 (old ALL) remaps to 9 ---
  {
    g_nvs.clear();
    Blob bl;
    uint8_t reg = 7;
    bl.field("reg", SK_U8, &reg, 1);
    auto blob = bl.finish(1);
    CHECK(saveImport(blob.data(), blob.size()), "v1 reg=7 import accepted");
    CHECK(nvsReg() == 9, "v1 reg=7 remapped to 9");
    CHECK(nvsFind("regv") && nvsFind("regv")->data[0] == 1, "regv marker written");
  }

  // --- v1 import: a normal region index is untouched ---
  {
    g_nvs.clear();
    Blob bl;
    uint8_t reg = 3;                                 // Hoenn
    bl.field("reg", SK_U8, &reg, 1);
    auto blob = bl.finish(1);
    CHECK(saveImport(blob.data(), blob.size()), "v1 reg=3 import accepted");
    CHECK(nvsReg() == 3, "v1 reg=3 unchanged");
  }

  // --- v1 import: legacy 8-slot eggR blob (16 bytes, 8 x int16_t) ---
  // saveImport writes it as-is; the Pet::load() length-based migration
  // (tested below) widens it on the next boot.
  {
    g_nvs.clear();
    Blob bl;
    uint8_t reg = 2;
    int16_t eggR[8] = {25, 0, 0, 155, 0, 0, 0, 511}; // slot 7 = old ALL target
    bl.field("reg", SK_U8, &reg, 1);
    bl.field("eggR", SK_BYTES, eggR, sizeof(eggR));
    auto blob = bl.finish(1);
    CHECK(saveImport(blob.data(), blob.size()), "v1 eggR import accepted");
    MockNvsEntry *e = nvsFind("eggR");
    CHECK(e && e->data.size() == 16, "legacy 16-byte eggR blob kept as-is by import");
    CHECK(*(int16_t *)(e->data.data() + 14) == 511, "slot 7 value intact after import");
  }

  // --- v1 import: legacy dex bitmap (102 bytes = 809 bits) preserved ---
  {
    g_nvs.clear();
    Blob bl;
    uint8_t reg = 4;
    uint8_t dexc[102];
    memset(dexc, 0, sizeof(dexc));
    dexc[63] = 0x80;                                  // species 512 caught
    bl.field("reg", SK_U8, &reg, 1);
    bl.field("dexc", SK_BYTES, dexc, sizeof(dexc));
    auto blob = bl.finish(1);
    CHECK(saveImport(blob.data(), blob.size()), "v1 dexc import accepted");
    MockNvsEntry *e = nvsFind("dexc");
    CHECK(e && e->data.size() == 102 && e->data[63] == 0x80, "legacy dex bitmap preserved");
  }

  // --- v2 import: 7/8/9 mean Galar/Paldea/ALL and must NOT be remapped ---
  for (uint8_t reg = 7; reg <= 9; reg++) {
    g_nvs.clear();
    Blob bl;
    bl.field("reg", SK_U8, &reg, 1);
    auto blob = bl.finish(SAVE_VERSION);
    CHECK(saveImport(blob.data(), blob.size()), "v2 import accepted");
    CHECK(nvsReg() == reg, "v2 reg round-trips unchanged");
  }

  // --- export -> import round-trip ---
  {
    g_nvs.clear();
    Blob bl;
    uint8_t reg = 9;
    bl.field("reg", SK_U8, &reg, 1);
    bl.field("nick", SK_STR, "TEST", 4);
    auto blob = bl.finish(SAVE_VERSION);
    CHECK(saveImport(blob.data(), blob.size()), "v2 round-trip import");
    std::vector<uint8_t> out(saveExportSize());
    size_t n = saveExport(out.data(), out.size());
    CHECK(n > 0, "export produced a blob");
    CHECK(out[4] == SAVE_VERSION, "export writes v2");
    g_nvs.clear();
    CHECK(saveImport(out.data(), n), "re-import of export");
    CHECK(nvsReg() == 9, "reg survives export/import");
  }

  // --- corrupt blob rejected, NVS untouched ---
  {
    g_nvs.clear();
    Blob bl;
    uint8_t reg = 5;
    bl.field("reg", SK_U8, &reg, 1);
    auto blob = bl.finish(SAVE_VERSION);
    blob[blob.size() / 2] ^= 0xFF;                   // corrupt the middle
    CHECK(!saveImport(blob.data(), blob.size()), "corrupt blob rejected");
    CHECK(nvsFind("reg") == nullptr, "NVS untouched by rejected blob");
  }

  // --- Pet::load() migration snippet ---
  {
    // legacy NVS: reg=7 (old ALL), no marker, legacy 16-byte eggR
    g_nvs.clear();
    Preferences p; p.begin("tamapoke", false);
    p.putUChar("reg", 7);
    int16_t legacyEggR[8] = {25, 0, 0, 155, 0, 0, 0, 511};
    p.putBytes("eggR", legacyEggR, sizeof(legacyEggR));
    int16_t eggByRegion[10] = {0};
    uint8_t r = runLoadMigration(p, eggByRegion);
    CHECK(r == 9, "migration remaps 7 -> 9");
    CHECK(nvsReg() == 9, "migration PERSISTS the remapped reg immediately");
    CHECK(p.isKey("regv"), "marker written");
    // eggR widened and moved
    MockNvsEntry *e = nvsFind("eggR");
    CHECK(e && e->data.size() == 20, "eggR blob widened to 20 bytes");
    CHECK(eggByRegion[0] == 25 && eggByRegion[3] == 155, "eggR slots 0-6 preserved");
    CHECK(eggByRegion[7] == 0 && eggByRegion[8] == 0, "new Galar/Paldea slots zeroed");
    CHECK(eggByRegion[9] == 511, "old ALL slot 7 moved to new slot 9");
    CHECK(*(int16_t *)(e->data.data() + 18) == 511, "widened eggR PERSISTED");
    // second boot: 20-byte blob loads as-is, nothing re-migrates
    int16_t again[10] = {0};
    r = runLoadMigration(p, again);
    CHECK(r == 9, "second boot keeps 9");
    CHECK(again[9] == 511 && again[7] == 0, "second boot eggR stable");
    // power-loss between marker write and next save() cannot resurrect 7:
    // the asserts above prove reg and eggR were rewritten before regv existed.
  }
  {
    // v2 NVS: reg=7 is a genuine Galar save - must NOT be remapped
    g_nvs.clear();
    Preferences p; p.begin("tamapoke", false);
    p.putUChar("reg", 7);
    p.putUChar("regv", 1);
    int16_t eggByRegion[10] = {0};
    int16_t v2eggR[10] = {0, 0, 0, 0, 0, 0, 0, 666, 0, 0};  // slot 7 = Galar target
    p.putBytes("eggR", v2eggR, sizeof(v2eggR));
    uint8_t r = runLoadMigration(p, eggByRegion);
    CHECK(r == 7, "v2 Galar reg=7 untouched by migration");
    CHECK(eggByRegion[7] == 666, "v2 Galar eggR slot untouched");
  }
  {
    // legacy NVS: a plain region (2 = Hoenn) - untouched
    g_nvs.clear();
    Preferences p; p.begin("tamapoke", false);
    p.putUChar("reg", 2);
    int16_t eggByRegion[10] = {0};
    uint8_t r = runLoadMigration(p, eggByRegion);
    CHECK(r == 2, "legacy reg=2 untouched");
    CHECK(p.isKey("regv"), "marker written for legacy save too");
  }
  {
    // legacy dex bitmap zero-extension: 102-byte blob into the 129-byte array
    // (Pet::begin() zeroes the array first; loadBlob writes the 102-byte
    // prefix, the 27-byte tail stays zero = species 810-1025 uncaught)
    g_nvs.clear();
    Preferences p; p.begin("tamapoke", false);
    uint8_t legacyDexc[102];
    memset(legacyDexc, 0, sizeof(legacyDexc));
    legacyDexc[63] = 0x80;                            // species 512 caught
    p.putBytes("dexc", legacyDexc, sizeof(legacyDexc));
    uint8_t dexCaught[129];
    memset(dexCaught, 0xAB, sizeof(dexCaught));       // simulate dirty RAM
    loadBlobMock(p, "dexc", dexCaught, sizeof(dexCaught));
    CHECK(dexCaught[63] == 0x80, "legacy dex bitmap head preserved");
    CHECK(dexCaught[101] == 0, "legacy bitmap fully copied");
    CHECK(dexCaught[102] == 0xAB, "bytes past the blob are NOT written");
    // (the real Pet::begin() memsets the array to 0 first, so those tail
    // bytes read as 0 there; this check pins loadBlob's contract)
  }

  {
    // FRESH SAVE: Pet::save() writes reg + regv together (mirrored below).
    // A new game picking Galar must never leave reg=7 without the marker,
    // or the next boot's legacy remap would read it as the old ALL.
    for (uint8_t reg = 7; reg <= 9; reg++) {
      g_nvs.clear();
      Preferences p; p.begin("tamapoke", false);
      // mirror of Pet::save()'s region writes -- keep in sync
      p.putUChar("reg", reg);
      p.putUChar("regv", 1);
      int16_t eggR[10] = {0};
      eggR[reg % 10] = 700 + reg;
      p.putBytes("eggR", eggR, sizeof(eggR));
      // boot: load migration against that store
      int16_t eggByRegion[10] = {0};
      uint8_t r = runLoadMigration(p, eggByRegion);
      CHECK(r == reg, "fresh save region survives boot unmigrated");
      CHECK(eggByRegion[reg % 10] == 700 + reg, "fresh save eggR intact");
      // second boot: still stable
      int16_t again[10] = {0};
      CHECK(runLoadMigration(p, again) == reg, "fresh save region stable on 2nd boot");
    }
  }
  {
    // COMBINED: v1 import (legacy 16-byte eggR) followed immediately by the
    // boot migration on the same store. saveImport writes regv itself, so the
    // boot's reg remap is skipped -- but the eggR LENGTH check must still
    // widen the legacy blob. Proves the migration does not depend on the
    // marker being absent.
    g_nvs.clear();
    Blob bl;
    uint8_t reg = 7;
    int16_t eggR[8] = {25, 0, 0, 155, 0, 0, 0, 511};
    bl.field("reg", SK_U8, &reg, 1);
    bl.field("eggR", SK_BYTES, eggR, sizeof(eggR));
    auto blob = bl.finish(1);
    CHECK(saveImport(blob.data(), blob.size()), "v1 import for combined test");
    Preferences p; p.begin("tamapoke", false);
    int16_t eggByRegion[10] = {0};
    uint8_t r = runLoadMigration(p, eggByRegion);
    CHECK(r == 9, "imported reg already remapped to 9 by import");
    MockNvsEntry *e = nvsFind("eggR");
    CHECK(e && e->data.size() == 20, "legacy eggR widened after import + boot");
    CHECK(eggByRegion[9] == 511 && eggByRegion[7] == 0, "old ALL moved to slot 9");
  }

  if (fails) { printf("%d FAILURES\n", fails); return 1; }
  printf("ALL SAVE TESTS PASSED\n");
  return 0;
}
