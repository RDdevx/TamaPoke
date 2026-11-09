// Trade protocol test: two Links cross-wired in one process over a transport
// that drops frames, and a cut that can land at ANY moment.
//
// The property under test is not "the trade completes" -- that is the easy
// half. It is "no creature is ever lost": at every possible instant the link
// could die, every creature still exists on at least one device.
#include "../link.h"
#include "../party.h"
#include "../dex.h"
#include <set>
#include <deque>
#include <vector>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Net;
struct Side {
  Link l;
  std::set<int> inv;     // dex numbers held (a stand-in for the party + box)
  int mine = 0;          // the dex we offered
  int stores = 0, releases = 0;
  Net *net = nullptr;
  int idx = 0;
};
struct Pkt { int to; std::vector<uint8_t> b; };
struct Net {
  Side s[2];
  std::deque<Pkt> q;
  int lossPct = 0;
  bool cut = false;
  uint32_t now = 0;
};

static void txFn(void *ctx, const uint8_t *buf, uint8_t len) {
  Side *me = (Side *)ctx;
  Net &n = *me->net;
  if (n.cut) return;
  if ((rand() % 100) < n.lossPct) return;
  n.q.push_back({1 - me->idx, std::vector<uint8_t>(buf, buf + len)});
}

static void appStep(Side &s) {
  if (s.l.tradeWantsStore()) {
    PartyMon pm;
    CHECK(tradeUnpack(pm, s.l.tTheirs.mon), "unpack");
    s.inv.insert(pm.dex);
    s.stores++;
    s.l.tradeStored();
  }
  if (s.l.tradeWantsRelease()) {
    s.inv.erase(s.mine);
    s.releases++;
    s.l.tradeFinished();
  }
}

static void offer(Side &s, int dex) {
  PartyMon pm; pm.dex = dex; pm.level = 10;
  uint8_t b[TRADE_MON_BYTES];
  tradePack(b, pm);
  s.mine = dex;
  s.l.tradeOffer(b);
}

// One scenario. cutAt < 0 means never cut. Returns steps used.
static void run(int seed, int loss, int cutAt, bool expectComplete) {
  srand(seed);
  Net n;
  n.lossPct = loss;
  for (int i = 0; i < 2; i++) {
    n.s[i].net = &n; n.s[i].idx = i;
    n.s[i].l.beginTrade(i ? "BOB" : "ALICE");
    n.s[i].l.id = (uint16_t)(1000 + i * 77);
    n.s[i].l.send = txFn; n.s[i].l.ctx = &n.s[i];
  }
  n.s[0].inv = { 25 };    // Alice holds Pikachu
  n.s[1].inv = { 133 };   // Bob holds Eevee
  n.s[0].l.start();
  n.s[1].l.start();

  bool aOff = false, bOff = false, aAcc = false, bAcc = false;
  for (int step = 0; step < 600; step++) {
    n.now += 50;
    if (cutAt >= 0 && step == cutAt) n.cut = true;
    // deliver everything queued this tick (in order; loss already applied)
    size_t cnt = n.q.size();
    for (size_t i = 0; i < cnt; i++) {
      Pkt p = n.q.front(); n.q.pop_front();
      if (n.cut) continue;
      n.s[p.to].l.onPacket(p.b.data(), (uint8_t)p.b.size());
    }
    for (int i = 0; i < 2; i++) { if (n.s[i].l.live()) n.s[i].l.tick(n.now); }
    // scripted users
    if (n.s[0].l.ready() && !aOff && step > 10) { offer(n.s[0], 25); aOff = true; }
    if (n.s[1].l.ready() && !bOff && step > 14) { offer(n.s[1], 133); bOff = true; }
    if (aOff && !aAcc && step > 30) aAcc = n.s[0].l.tradeAccept();
    if (bOff && !bAcc && step > 34) bAcc = n.s[1].l.tradeAccept();
    for (int i = 0; i < 2; i++) appStep(n.s[i]);
  }

  // THE property: nothing is ever lost, wherever the cut landed.
  std::set<int> all = n.s[0].inv;
  all.insert(n.s[1].inv.begin(), n.s[1].inv.end());
  CHECK(all.count(25) && all.count(133),
        "LOST a creature: seed %d loss %d cut %d (A has %zu, B has %zu)",
        seed, loss, cutAt, n.s[0].inv.size(), n.s[1].inv.size());
  // Neither side may ever end up with nothing either.
  CHECK(!n.s[0].inv.empty() && !n.s[1].inv.empty(), "a side is empty: seed %d cut %d", seed, cutAt);
  // Each side stores at most once, releases at most once.
  for (int i = 0; i < 2; i++) {
    CHECK(n.s[i].stores <= 1 && n.s[i].releases <= 1, "double apply side %d seed %d cut %d", i, seed, cutAt);
    // A release must never come without a store on the SAME side having
    // happened first -- otherwise a side could end empty-handed.
    CHECK(!(n.s[i].releases && !n.s[i].stores), "released without storing: side %d", i);
  }
  if (expectComplete) {
    CHECK(n.s[0].inv == std::set<int>({133}), "A should hold only Eevee (seed %d loss %d)", seed, loss);
    CHECK(n.s[1].inv == std::set<int>({25}), "B should hold only Pikachu (seed %d loss %d)", seed, loss);
    CHECK(n.s[0].l.tradeSettled() && n.s[1].l.tradeSettled(), "not settled seed %d loss %d", seed, loss);
  }
}

int main() {
  // 1. Clean air.
  run(1, 0, -1, true);
  // 2. Hostile air: a third of all frames dropped, many seeds.
  for (int seed = 0; seed < 200; seed++) run(seed, 33, -1, true);
  // 3. The link dies at EVERY possible moment, across clean and lossy air.
  for (int loss : { 0, 20, 40 })
    for (int cut = 0; cut < 200; cut++)
      for (int seed = 0; seed < 6; seed++) run(seed * 31 + cut, loss, cut, false);

  // 4. Only the offered creature is ever on the wire, and garbage is rejected.
  {
    PartyMon pm; uint8_t raw[TRADE_MON_BYTES];
    memset(raw, 0xFF, sizeof(raw));
    CHECK(!tradeUnpack(pm, raw), "dex 65535 must be rejected");
    memset(raw, 0, sizeof(raw));
    CHECK(!tradeUnpack(pm, raw), "dex 0 must be rejected");
    raw[0] = 25; raw[2] = 250; raw[6] = 200; raw[10] = 255; raw[14] = 'A'; raw[15] = 0x07; raw[16] = 0xC3;
    raw[26] = 255;
    CHECK(tradeUnpack(pm, raw), "valid dex");
    CHECK(pm.level <= MAX_LEVEL, "level clamped");
    CHECK(pm.ivAtk <= 31, "iv clamped");
    CHECK(pm.trAtk <= 70 + 30 * pm.ivAtk / 31, "training clamped to the IV ceiling");
    CHECK(pm.nick[1] == '?' && pm.nick[2] == '?', "non-printable nick bytes replaced");
    CHECK(pm.moves[0] == 0, "invalid move index zeroed");
  }

  // 5. Trade vs battle: refused, not a hang.
  {
    Net n; Side a, b;
    a.net = b.net = &n; a.idx = 0; b.idx = 1;
    a.l.beginTrade("A"); b.l.begin(true, "B");
    a.l.id = 5; b.l.id = 9;
    a.l.send = txFn; a.l.ctx = &a; b.l.send = txFn; b.l.ctx = &b;
    n.s[0].l.send = nullptr;
    a.l.start(); b.l.start();
    // route manually
    for (int i = 0; i < 6; i++) {
      size_t cnt = n.q.size();
      for (size_t k = 0; k < cnt; k++) {
        Pkt p = n.q.front(); n.q.pop_front();
        (p.to == 1 ? b : a).l.onPacket(p.b.data(), (uint8_t)p.b.size());
      }
    }
    CHECK(a.l.state == LINK_REFUSED || b.l.state == LINK_REFUSED, "mode mismatch must be refused");
  }

  printf(failures ? "\n%d FAILURES\n" : "\nall trade tests passed\n", failures);
  return failures ? 1 : 0;
}
