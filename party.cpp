#include "party.h"
#include <stdlib.h>
#include <string.h>
#include "dex.h"

Party party;

// Same NVS namespace as the pet on purpose: WIPE (Pet::factoryReset) calls
// clear() on it, and a factory reset that left the party behind would be a lie.
void Party::begin() {
  // Start from empty: getBytes() leaves the destination untouched when the key
  // is missing, so without this a reload after a wipe would keep showing the
  // old party out of RAM.
  for (auto &s : slots) s = PartyMon();
  prefs.begin("tamapoke", false);
  // The blob is raw structs, so growing PartyMon (moves[] was appended in v1.9)
  // changes its stride. Reading an older, shorter blob straight into the new
  // array would land slot 1 onward at the wrong offset and quietly invent a
  // party out of misaligned bytes -- and the dex-range check below would not
  // reliably catch it, since a stray byte is often a valid Pokedex number.
  // So migrate by length: copy each old record into the front of the new one
  // and leave moves[] zeroed for the learnset to fill in.
  // The record stride is inferred from the stored length, NEVER from
  // PARTY_SLOTS (the old code did `stored / PARTY_SLOTS`, which is right when
  // the stride grows and wrong when the slot count does). Two layouts are
  // recognised: records at our own stride (any count), or a pre-moves blob
  // written with PARTY_LEGACY_SLOTS shorter records. Anything else is left
  // empty rather than read misaligned.
  //
  // Records beyond PARTY_SLOTS (a save from the six-slot era) are NOT dropped:
  // they are moved to the box once it is loaded, below.
  PartyMon spill[PARTY_LEGACY_SLOTS];
  uint8_t spillN = 0;
  bool rewrite = false;          // true once the blob must be saved in the new layout
  size_t stored = prefs.getBytesLength("party");
  size_t stride = 0;
  if (stored && stored % sizeof(PartyMon) == 0) stride = sizeof(PartyMon);
  else if (stored && stored % PARTY_LEGACY_SLOTS == 0 &&
           stored < PARTY_LEGACY_SLOTS * sizeof(PartyMon))
    stride = stored / PARTY_LEGACY_SLOTS;
  if (stride) {
    // getBytes copies NOTHING when the stored blob exceeds the buffer, so the
    // read always goes through a temporary of the stored size.
    uint8_t *tmp = (uint8_t *)malloc(stored);
    if (tmp && prefs.getBytes("party", tmp, stored) == stored) {
      size_t n = stored / stride;
      size_t keep = stride < sizeof(PartyMon) ? stride : sizeof(PartyMon);
      for (size_t i = 0; i < n; i++) {
        PartyMon r;                              // moves[] etc. stay zeroed
        memcpy(&r, tmp + i * stride, keep);
        if (i < PARTY_SLOTS) slots[i] = r;
        else if (spillN < PARTY_LEGACY_SLOTS) spill[spillN++] = r;
      }
      rewrite = (stride != sizeof(PartyMon)) || n > PARTY_SLOTS;
    }
    free(tmp);
  }
  // a blob written by an older/newer build could hold nonsense; drop anything
  // that is not a real Pokedex number rather than indexing DEX_TBL with it
  for (auto &s : slots) {
    if (s.dex < 1 || s.dex > DEX_COUNT) s.dex = 0;
    s.nick[sizeof(s.nick) - 1] = 0;
  }
  // The box is a separate key and simply absent on an older save, which leaves
  // it zeroed -- exactly what an empty box is.
  for (auto &s : box) s = PartyMon();
  size_t boxStored = prefs.getBytesLength("box");
  if (boxStored == sizeof(box)) {
    prefs.getBytes("box", box, sizeof(box));
  } else if (boxStored > sizeof(box) && boxStored % sizeof(PartyMon) == 0) {
    // Same as the party above: a later build with more box slots. Keep the
    // first BOX_SLOTS rather than dropping the whole box on the floor, which is
    // what happened before -- getBytes refuses an oversized blob outright.
    uint8_t *tmp = (uint8_t *)malloc(boxStored);
    if (tmp) {
      if (prefs.getBytes("box", tmp, boxStored) == boxStored)
        memcpy(box, tmp, sizeof(box));
      free(tmp);
    }
  }
  for (auto &s : box) {
    if (s.dex < 1 || s.dex > DEX_COUNT) s.dex = 0;
    s.nick[sizeof(s.nick) - 1] = 0;
  }
  // The party shrank from six to five banked slots: whoever sat in the sixth
  // goes to the box rather than vanishing. If the box has no room the stored
  // blob is left untouched (so nothing is destroyed by loading) and this is
  // tried again on the next boot.
  bool allPlaced = true;
  for (uint8_t i = 0; i < spillN; i++) {
    if (spill[i].dex < 1 || spill[i].dex > DEX_COUNT) continue;   // empty/junk
    if (!boxAdd(spill[i])) { allPlaced = false; break; }
  }
  if (rewrite && allPlaced) save();   // rewrite once, in the current layout
}

void Party::save() {
  prefs.putBytes("party", slots, sizeof(slots));
}

void Party::boxSave() {
  prefs.putBytes("box", box, sizeof(box));
}

uint8_t Party::boxCount() const {
  uint8_t n = 0;
  for (auto &s : box)
    if (!s.empty()) n++;
  return n;
}

int Party::boxFirstFree() const {
  for (int i = 0; i < BOX_SLOTS; i++)
    if (box[i].empty()) return i;
  return -1;
}

bool Party::boxAdd(const PartyMon &m) {
  int i = boxFirstFree();
  if (i < 0) return false;
  box[i] = m;
  boxSave();
  return true;
}

void Party::boxReleaseAt(uint8_t i) {
  if (i >= BOX_SLOTS) return;
  box[i] = PartyMon();
  boxSave();
}

void Party::swapPartyBox(uint8_t partyIdx, uint8_t boxIdx) {
  if (partyIdx >= PARTY_SLOTS || boxIdx >= BOX_SLOTS) return;
  PartyMon t = slots[partyIdx];
  slots[partyIdx] = box[boxIdx];
  box[boxIdx] = t;
  save();
  boxSave();
}

uint8_t Party::count() const {
  uint8_t n = 0;
  for (auto &s : slots)
    if (!s.empty()) n++;
  return n;
}

int Party::firstFree() const {
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (slots[i].empty()) return i;
  return -1;
}

bool Party::add(const PartyMon &m) {
  int i = firstFree();
  if (i < 0) return false;
  slots[i] = m;
  save();
  return true;
}

void Party::replaceAt(uint8_t i, const PartyMon &m) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = m;
  save();
}

void Party::releaseAt(uint8_t i) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = PartyMon();
  save();
}

// Mirrors calcStat() in pet.cpp: base + level + IV contribution + training.
// Kept in step with it by hand; there is no shared home for it that both the
// live pet and a frozen party member could use without dragging Pet in here.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint16_t lvl, uint8_t tr) {
  return (uint16_t)base + lvl + (uint32_t)iv * lvl / 100 + tr;
}

uint16_t Party::atkOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bAtk, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::defOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bDef, m.ivDef, m.level, m.trDef);
}
uint16_t Party::speOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpe, m.ivSpe, m.level, m.trSpe);
}
uint16_t Party::vitOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bHp, m.ivHp, m.level, 10);
}
// Special reuses the physical IV and training, same rule as Pet::spaStat().
uint16_t Party::spaOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpA, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::spdOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpD, m.ivDef, m.level, m.trDef);
}
