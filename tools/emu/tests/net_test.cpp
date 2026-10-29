// The UDP transport, over two real processes. link_test and lan_test drive
// the Link protocol in-process with the radio swapped for a function pointer;
// this one runs it through the actual socket the GUI emulator battles over --
// multicast, the self-filter, the peer lock. fork() gives each side its own
// singletons and its own socket, which is exactly what two emulator
// instances on one machine are.
#include "Arduino.h"
#include "link.h"
#include "linknow.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <chrono>

// clock.cpp's millis() gestures off these; nothing is touching anything here
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t g_seed = 7;   // pet.cpp rolls IVs off this

// the sketch's millis() lives in clock.cpp, which this test does not link;
// tick() only wants a monotonic millisecond count
static uint32_t nowMs() {
  static auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now() - t0).count();
}

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// HOST advertises SHELLDER (90), GUEST advertises ALAKAZAM (65)
static LinkMon mon(int16_t dex, uint8_t lvl, const char *nm){
  Pet p; p.dbgHatchAs(dex,false);
  p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  Combatant c; combatantFromPet(c,p);
  snprintf(c.name,sizeof(c.name),"%s",nm);
  LinkMon m; linkMonFrom(m,c); return m;
}

// one side of the pairing. Returns true if it reached READY holding the
// other side's creature. The squad exchange rides on the resend timer, so a
// side that has reached READY must KEEP ticking for a while -- in the real
// emulator both instances run their loop() until the battle is over, and a
// peer that exits at READY never delivers its squad.
static bool runPeer(bool host) {
  Link l;
  l.begin(host, host ? "HOST" : "GUEST");
  l.addMon(host ? mon(90,30,"SHELL") : mon(65,30,"SPOON"));
  if (!linkNowBegin(&l)) return false;
  l.start();
  uint32_t t0 = nowMs();
  while (!l.ready() && nowMs() - t0 < 5000) {
    linkNowPoll();
    l.tick(nowMs());
    usleep(2000);
  }
  // both sides hold on: resends flush the last of the exchange
  uint32_t t1 = nowMs();
  while (nowMs() - t1 < 1500) {
    linkNowPoll();
    l.tick(nowMs());
    usleep(2000);
  }
  bool ok = l.ready() && l.theirsN == 1 &&
            l.theirs[0].dex == (host ? 65 : 90);
  linkNowEnd();
  return ok;
}

int main(){
  pid_t pid = fork();
  if (pid == 0) _exit(runPeer(false) ? 0 : 1);   // the child is the guest
  bool hostOk = runPeer(true);
  int st = 0;
  waitpid(pid, &st, 0);
  bool guestOk = WIFEXITED(st) && WEXITSTATUS(st) == 0;
  ck(hostOk, "the host pairs and reaches READY over a real socket");
  ck(guestOk, "and so does the guest, holding the host's squad");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
