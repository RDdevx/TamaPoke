// A LAN battle, over the real UDP socket, between two real firmware
// processes. link_test and lan_test drive the protocol in-process; net_test
// pairs two processes but stops at READY. This one runs the actual battle:
// two forked copies of the firmware, each with its own NVS (the guest chdirs
// first), pairing over the socket the GUI emulator uses, fighting with the
// host resolving turns and the guest taking the ending off the wire -- and
// the two processes must agree on who won.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

uint32_t g_seed=31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void FakeESP::restart(){exit(0);}

void setup(); void loop(); void render();
void startLinkBattle();
void battleTap(int16_t x, int16_t y);
bool linkNowBegin(Link *); void linkNowEnd(); void linkNowPoll(); bool linkNowUp();
extern Pet pet;
extern Link lan;
extern bool battleOpen, btlOver, btlWon, btlLink;
extern uint8_t btlMsgCount, btlMyAct;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// HOST runs SHELLDER (90), GUEST runs ALAKAZAM (65), both at level 40
// 1: the run reached the end of the fight; 0/1 in the low bit: who won
static int runSide(bool host) {
  if (!host) {                     // separate NVS, as --save would give you
    mkdir("lan2p_guest", 0755);
    if (chdir("lan2p_guest") != 0) return 0xE0;
  }
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.isEgg()) pet.dbgHatchAs(host ? 90 : 65, false);
  pet.ageMinutes = 40UL*MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  render();

  // pair, through the real transport
  lan.begin(host, host ? "HOST" : "GUEST");
  {
    Pet p; p.dbgHatchAs(host ? 90 : 65, false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=25;
    p.ageMinutes = 40UL*MINUTES_PER_LEVEL;
    p.relearnFromLevel();
    Combatant c; combatantFromPet(c,p);
    LinkMon m; linkMonFrom(m,c);
    lan.addMon(m);
  }
  if (!linkNowBegin(&lan)) return 0xE1;
  lan.start();
  uint32_t t0 = millis();
  while (!lan.ready() && millis() - t0 < 8000) { loop(); render(); }
  if (!lan.ready()) return 0xE2;

  // fight. Moves are chosen the way a player does: two taps on the first
  // grid cell (FIGHT, then the move itself) -- that is what latches the act
  // and, on the guest, what sends it.
  startLinkBattle();
  if (!battleOpen || !btlLink) return 0xE3;
  int guard = 0;
  while (!btlOver && guard++ < 4000) {
    btlMsgCount = 0;                       // what a player's taps would clear
    if (!btlMyAct) { battleTap(149, 296); battleTap(149, 296); }
    loop();
    render();
    usleep(500);
  }
  if (!btlOver) return 0xE4;
  return btlWon ? 1 : 2;
}

int main(){
  pid_t pid = fork();
  if (pid == 0) {
    // the guest speaks only through its exit code: its output would be
    // indistinguishable from ours once the streams interleave
    int r = runSide(false);
    linkNowEnd();
    _exit(r);
  }
  int hostR = runSide(true);
  linkNowEnd();
  int st = 0;
  waitpid(pid, &st, 0);
  int guestR = WIFEXITED(st) ? WEXITSTATUS(st) : 0xEE;

  ck(hostR == 1 || hostR == 2, "the host's fight runs to an end");
  ck(guestR == 1 || guestR == 2, "and so does the guest's");
  ck((hostR == 1) != (guestR == 1),
     "and they agree on the outcome (one won, one lost)");
  printf("      host=0x%x guest=0x%x\n", hostR, guestR);
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
