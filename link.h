#pragma once
#include <stdint.h>
#include "battle.h"
#include "trainers.h"   // TRAINER_TEAM_MAX: a link team is the same size

// Peer-to-peer battles.
//
// ONE DEVICE IS AUTHORITATIVE. That is forced, not chosen: battle.cpp makes 11
// random() calls a turn -- crit, damage spread, accuracy, ailment procs,
// multi-hit count, confusion, thaw, wake, speed ties -- so two devices
// resolving independently desync inside a single turn. A shared seed only holds
// until the two builds differ by one call. The host runs battleAct(); the guest
// sends an action and renders the result it is handed, and therefore needs no
// game logic at all.
//
// THE RADIO DROPS FRAMES. ESP-NOW is best effort, so nothing here may assume a
// packet arrived. Three rules cover it:
//
//   1. Every exchange is stamped with a turn number. A repeat of a turn already
//      handled is ignored, so a resend can never be mistaken for a new choice.
//   2. Whatever we last said is resent until something supersedes it. That is
//      what makes a lost frame a delay rather than a permanent stall.
//   3. A LinkResult carries ABSOLUTE state, never deltas. A guest that misses a
//      turn entirely still lands on the right numbers from the next one -- the
//      loss costs narration, not correctness.
//
// And every wait has a deadline: a peer that goes out of range or powers off
// ends as LINK_LOST with a message, never as a hang.
//
// The transport is a function pointer rather than a direct ESP-NOW call so the
// protocol can be tested: two Links cross-wired in one process exercise the
// whole handshake without a radio, and a deliberately lossy transport exercises
// all of the above. Only the radio itself is unverifiable here.

#define LINK_PROTO 3        // bump on ANY wire change; a mismatch is refused
#define LINK_MAX_PAYLOAD 200
#define LINK_NAME_LEN 12

#define LINK_RESEND_MS 400
// Resends are jittered, and that is not decoration. A fixed interval can lock
// step with a periodic interferer -- a beacon, a microwave -- so that the same
// packet in the cycle is lost every single time and the resends never help.
// The lossy test reproduces exactly that: with one frame in three dropped and a
// fixed interval, one squad packet was destroyed on all ten attempts.
#define LINK_JITTER_MS 200
#define LINK_PAIR_TIMEOUT_MS 20000    // waiting for a peer that may not exist
#define LINK_BATTLE_TIMEOUT_MS 12000  // mid-fight, where one is known to be there

// An action is a move slot, or a switch with the high bit set. One message
// rather than two, so turn matching and resend have a single path -- which is
// the place a second message type would most easily drift out of step.
//
// A move is stored as slot+1 so that 0 can keep meaning "nothing chosen yet".
// Storing the slot raw would make move slot 0 -- the first move every creature
// has -- indistinguishable from silence.
#define LINK_ACT_SWITCH 0x80
#define LINK_ACT_MOVE(i) ((uint8_t)((i) + 1))
#define LINK_ACT_SWITCH_TO(i) ((uint8_t)(LINK_ACT_SWITCH | ((i) & 0x0F)))
#define LINK_ACT_IS_SWITCH(a) (((a) & LINK_ACT_SWITCH) != 0)
#define LINK_ACT_SLOT(a) \
  ((uint8_t)(LINK_ACT_IS_SWITCH(a) ? ((a) & 0x0F) : ((a) - 1)))

enum LinkState : uint8_t {
  LINK_OFF = 0,
  LINK_LISTENING,    // waiting for a peer
  LINK_HANDSHAKE,    // hello sent, waiting for theirs
  LINK_SQUADS,       // teams being exchanged
  LINK_READY,        // both known; an action may be chosen
  LINK_WAITING,      // acted, waiting on the other side
  LINK_DONE,         // somebody won
  LINK_REFUSED,      // incompatible -- loudly, never a silent desync
  LINK_LOST,         // peer went quiet, or said goodbye
};

enum LinkMsg : uint8_t {
  LM_HELLO = 1,
  LM_SQUAD,
  LM_ACT,        // guest -> host: a move slot or a switch, stamped with a turn
  LM_RESULT,     // host -> guest: what happened, stamped with the same turn
  LM_END,
  LM_BYE,        // leaving on purpose, so the peer need not wait for a timeout
  LM_REMATCH,    // go again with the same squads
  LM_TRADE,      // trade mode: our whole trade state, resent until superseded
};

// A creature on the wire. Deliberately not `Combatant` itself: that carries
// live battle state (stages, ailments, timers) which only the host owns.
struct LinkMon {
  int16_t dex;
  uint8_t level;
  uint16_t maxHp;
  uint16_t base[SI_COUNT];
  uint8_t moves[MOVE_SLOTS];
  uint8_t shiny;
  char name[LINK_NAME_LEN];
};

// What the host tells the guest happened. Everything the guest needs to render
// a turn, and nothing it could use to resolve one -- it has no say in the
// outcome, which is the whole point of one side being authoritative.
//
// Every field is ABSOLUTE, not a delta. That is what lets a guest which missed
// a turn recover from the next one instead of drifting.
struct LinkResult {
  uint16_t hostHp, guestHp;
  uint8_t hostAil, guestAil;
  uint8_t hostMove, guestMove;
  uint16_t hostDmg, guestDmg;
  uint8_t hostIdx, guestIdx;   // which squad member is out on each side
  uint8_t flags;               // bit0 crit, bit1 super, bit2 a faint happened
};

// ---------------------------------------------------------------------------
// TRADING. Same radio, same pairing, a different conversation.
//
// THE RULE THAT MATTERS: a creature is only ever DELETED after the other side
// has confirmed it stored its copy. Nothing can be lost by a dropped frame, a
// dead battery or a walked-off partner -- the worst outcome is that BOTH sides
// end up holding both creatures (a duplicate). That is the opposite trade-off
// from the classic games, and it is deliberate: no network can make a swap of
// two items atomic (the "two generals" problem), so one failure mode has to be
// chosen, and a duplicate is the one that costs nobody a creature.
//
// Each side keeps ONE state packet live and resends it until it changes. It
// carries absolute state, never an event, and a version counter so a late
// duplicate can never move anybody backwards -- the same idea as the battle
// turn number above.
//
//   BROWSE -> OFFER -> ACCEPT -> STORED -> DONE
//
//   OFFER   I have picked a creature to give (its bytes ride in the packet)
//   ACCEPT  I have seen your offer and I take it (`seen` names which offer)
//   STORED  I agreed with you on this exact pair AND I have written your
//           creature to my own save. Only now is it safe for YOU to delete yours.
//   DONE    I have seen that you stored mine, and I deleted mine.
//
// Offers carry an id so an ACCEPT can only ever be matched to the offer it was
// given for: if you change your mind in flight, a stale ACCEPT names an old id
// and is ignored rather than trading the wrong creature.
// ---------------------------------------------------------------------------
#define TRADE_MON_BYTES 30
enum TradePhase : uint8_t { TP_BROWSE = 0, TP_OFFER, TP_ACCEPT, TP_STORED, TP_DONE };

struct TradeSide {
  uint8_t ver = 0;        // bumped by its owner on EVERY change; newer wins
  bool known = false;     // (peer side only) at least one packet has arrived
  uint8_t phase = TP_BROWSE;
  uint8_t oid = 0;        // identifies the creature currently offered
  uint8_t seen = 0;       // the peer's oid that this side accepted
  uint8_t mon[TRADE_MON_BYTES] = { 0 };
};

struct PartyMon;          // party.h -- kept out of here so link.cpp stays pure protocol

struct Link {
  uint8_t state = LINK_OFF;
  bool isHost = false;
  uint8_t protoTheirs = 0;
  uint16_t buildTheirs = 0;
  uint16_t id = 0;             // breaks the tie when both sides offer to host
  uint16_t peerId = 0;
  char peerName[LINK_NAME_LEN] = "";

  LinkMon mine[TRAINER_TEAM_MAX];
  uint8_t mineN = 0;
  LinkMon theirs[TRAINER_TEAM_MAX];
  uint8_t theirsN = 0;
  uint8_t theirsExpected = 0;  // from their hello, so a lost SQUAD is detectable
  uint16_t theirsGot = 0;      // which slots have actually arrived

  uint8_t turn = 0;            // the exchange both sides are on
  uint8_t pendingAct = 0;      // host: the guest's action for `turn`, 0 = none
  bool youWon = false;

  // guest: the last turn the host resolved. `resultNew` is cleared by whoever
  // renders it, so a dropped or duplicated frame cannot replay a turn twice.
  uint8_t result[LINK_MAX_PAYLOAD];
  uint8_t resultN = 0;
  bool resultNew = false;

  // what we last said, kept so it can be said again until superseded
  uint8_t tx[2 + LINK_MAX_PAYLOAD];
  uint8_t txN = 0;
  bool txLive = false;
  uint32_t txAt = 0;
  uint32_t lastRx = 0;
  uint8_t resendSeq = 0;       // varies the resend interval, see LINK_JITTER_MS

  // trade mode (see the block above). `tMine` is what we say, `tTheirs` is the
  // newest thing we have heard them say.
  bool tradeMode = false;
  TradeSide tMine, tTheirs;
  bool sawRx = false;          // onPacket has no clock; tick() stamps it
  bool armed = false;

  // set by whoever owns the radio; ctx lets a test route two Links to each other
  void (*send)(void *ctx, const uint8_t *buf, uint8_t len) = nullptr;
  void *ctx = nullptr;

  void begin(bool host, const char *myName);
  void beginTrade(const char *myName);   // pair for a trade instead of a fight
  void addMon(const LinkMon &m);
  void start();                       // announce: hello, then squad
  void onPacket(const uint8_t *buf, uint8_t len);
  void tick(uint32_t now);            // resend + timeout; call once a frame

  void sendAct(uint8_t act);          // guest -> host: a move slot or a switch
  void sendResult(const uint8_t *blob, uint8_t len);   // host -> guest
  void sendEnd(bool hostWon);
  void sendBye();                     // leaving deliberately
  void sendRematch();
  void rearm();                       // same squads, fresh fight

  // --- trading ---
  // Offer (or change the offer to) a creature, already packed by tradePack().
  // Only before we have accepted: after that the offer is what we agreed to.
  bool tradeOffer(const uint8_t *mon);
  // Take their current offer. False if they have not offered anything.
  bool tradeAccept();
  // Back to browsing. Refused once we have STORED -- by then it is committed.
  bool tradeCancel();
  // We and they accepted the same pair: the app should now WRITE THEIR creature
  // to its save and then call tradeStored().
  bool tradeWantsStore() const;
  void tradeStored();
  // They have stored ours: the app may now delete ours, then call tradeFinished().
  bool tradeWantsRelease() const;
  void tradeFinished();
  // Past the point where leaving would risk a duplicate.
  // We stored theirs but they walked back from the deal before storing ours.
  // We keep both (a duplicate, never a loss); tradeReset() returns us to
  // browsing so the pair can carry on.
  bool tradeAborted() const;
  void tradeReset();
  bool tradeCommitted() const { return tradeMode && tMine.phase >= TP_STORED; }
  bool tradeSettled() const {
    return tradeMode && tMine.phase == TP_DONE && tTheirs.phase == TP_DONE;
  }

  bool ready() const { return state == LINK_READY; }
  bool live() const {
    return state != LINK_OFF && state != LINK_REFUSED && state != LINK_LOST;
  }
  // the host's own action is its own business; this is the guest's, and it
  // only counts once it has actually arrived
  bool hasPeerAct() const { return isHost && pendingAct != 0; }
};

// Everything that must match for two builds to narrate the same fight. NOT the
// firmware version: a build differing only in, say, a colour is perfectly
// compatible and refusing it would be noise. These are the things that would
// actually make one device read a packet differently from the other.
uint16_t linkBuildTag();

// Builds the wire form of a combatant, so both ends agree on what a creature is.
// linkMonTo VALIDATES: everything it produces is safe to index a table with,
// whatever actually arrived over the air.
void linkMonFrom(LinkMon &out, const Combatant &c);
void linkMonTo(Combatant &out, const LinkMon &m);
uint8_t linkSafeMove(uint8_t m);      // 0 unless it is a real move

// A banked creature on the wire: a fixed, explicit byte layout rather than a
// struct memcpy, so padding and a future member reorder cannot change what the
// other device reads.
void tradePack(uint8_t *out, const PartyMon &m);
// Everything off the air is untrusted. Fills `out` with something that is safe
// to index tables with and to print, whatever the bytes were. False if it
// cannot be a real creature at all (dex out of range).
bool tradeUnpack(PartyMon &out, const uint8_t *in);
