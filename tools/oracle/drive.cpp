// Oracle driver (kept outside the repo; links GPL gambatte). Usage: drive rom.gb script.txt
// Script lines: "wait N" (frames), "press BTN[+BTN] N" (hold N frames then 2 released), "save" (flush SRAM to rom.sav)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include "gambatte.h"
#include "inputgetter.h"
struct In : gambatte::InputGetter { unsigned p = 0; unsigned operator()() override { return p; } };
gambatte::GB gb; In in;
FILE* logf=nullptr; unsigned long frames=0;
void onw(char p,char d,unsigned long c){ if(logf && (unsigned char)p>=0x10 && (unsigned char)p<0x40) fprintf(logf,"%lu %02X %02X\n",frames,(unsigned char)p,(unsigned char)d);}
void onl(){}
void frame() { ++frames; size_t s = 35112; static gambatte::uint_least32_t buf[35112 + 2064]; gb.runFor(0, 0, buf, s); }
void wait(int n) { while (n-- > 0) frame(); }
unsigned parse(const std::string& s) {
  unsigned m = 0; std::stringstream ss(s); std::string t;
  while (std::getline(ss, t, '+')) {
    if (t == "A") m |= 1; else if (t == "B") m |= 2; else if (t == "SELECT") m |= 4; else if (t == "START") m |= 8;
    else if (t == "RIGHT") m |= 0x10; else if (t == "LEFT") m |= 0x20; else if (t == "UP") m |= 0x40; else if (t == "DOWN") m |= 0x80;
  }
  return m;
}
int main(int argc, char** argv) {
  if (argc < 3) return 1;
  gb.setInputGetter(&in); gb.setWriteHandler(onw); gb.setLcdHandler(onl); if(getenv("REGLOG")) logf=fopen(getenv("REGLOG"),"w");
  if (gb.load(argv[1], gb.FORCE_DMG)) { fprintf(stderr, "load failed\n"); return 1; }
  std::ifstream f(argv[2]); std::string line;
  while (std::getline(f, line)) {
    size_t h = line.find('#'); if (h != std::string::npos) line.resize(h);
    std::stringstream ss(line); std::string c, a; int n = 0; ss >> c >> a >> n;
    if (c == "wait") wait(atoi(a.c_str()));
    else if (c == "press") { in.p = parse(a); wait(n ? n : 4); in.p = 0; wait(4); }
    else if (c == "save") gb.saveSavedata();
  }
  gb.saveSavedata();
}
