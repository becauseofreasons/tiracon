// C++ side of test/compare.js: runs a scenario on the core and prints the same checkpoints.
//   tiracon_compare <scenario.txt> <rom.bin>
#include "board.h"
#include "synth.h"
#include "device.h"
#include "firmware.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

using namespace t6v;

struct Event { int ms; bool midi; std::vector<uint8_t> bytes; int code; bool down; };

static std::string g(double x) { char buf[64]; std::snprintf(buf, sizeof buf, "%.9g", x); return buf; }

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "tiracon_compare <scenario.txt> <rom.bin>\n"); return 2; }
  std::ifstream f(argv[1]);
  int every = 50, end = 1000;
  std::string serial = "1266";
  bool ideal = false;
  std::vector<Event> events;
  std::string line;
  while (std::getline(f, line)) {
    size_t h = line.find('#');
    if (h != std::string::npos) line = line.substr(0, h);
    std::istringstream is(line);
    std::string w0, cmd;
    if (!(is >> w0)) continue;
    if (w0 == "every") { is >> every; continue; }
    if (w0 == "device") { is >> serial; continue; }
    if (w0 == "ideal") { ideal = true; continue; }
    int ms = std::atoi(w0.c_str());
    is >> cmd;
    if (cmd == "end") end = ms;
    else if (cmd == "midi") {
      Event e{ ms, true, {}, 0, false };
      std::string x;
      while (is >> x) e.bytes.push_back(uint8_t(std::strtol(x.c_str(), nullptr, 16)));
      events.push_back(e);
    } else if (cmd == "blp") {
      std::string c, d; is >> c >> d;
      events.push_back({ ms, false, {}, int(std::strtol(c.c_str(), nullptr, 16)), d == "1" });
    }
  }
  std::ifstream rf(argv[2], std::ios::binary);
  std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());

  auto b = std::make_unique<Board>();
  b->loadRom(rom.data(), int(rom.size()));
  std::memcpy(b->ram, factoryRam(), 0x800);
  Synth synth(48000);
  synth.setPhases(0);
  DeviceProfile dev = makeDevice(serial, ideal);
  b->setDevice(&dev.board);
  synth.setDevice(&dev.synth);
  b->reset();

  float frame[FRAME_LEN], buf[48];
  double sumsq = 0;
  size_t ev = 0;
  for (int ms = 0; ms < end; ms++) {
    while (ev < events.size() && events[ev].ms <= ms) {
      const Event& e = events[ev++];
      if (e.midi) b->midiIn(e.bytes.data(), int(e.bytes.size())); else b->setBlpKey(e.code, e.down);
    }
    b->runUntil(int64_t(ms + 1) * 4000);
    b->audioFrame(frame);
    synth.setFrame(frame);
    synth.process(buf, 48);
    for (int k = 0; k < 48; k++) sumsq += double(buf[k]) * double(buf[k]);
    if ((ms + 1) % every == 0) {
      uint32_t hsh = 2166136261u;
      for (int i = 0; i < 0x800; i++) { hsh ^= b->ram[i]; hsh *= 16777619u; }
      std::printf("C %d %lld %d %d %d %d %x %lld %u %u %u\n", ms + 1, (long long)b->now, b->cpu.pc, b->cpu.sp, b->cpu.a, b->cpu.f,
                  hsh, (long long)b->idleCycles, b->ctc.ch[3].src.count, b->ctc.ch[3].lost, b->dart.overruns);
      std::printf("F");
      for (int i = 0; i < FRAME_LEN; i++) std::printf(" %s", g(frame[i]).c_str());
      std::printf("\nA %s %s\n", g(sumsq).c_str(), g(buf[47]).c_str());
      sumsq = 0;
    }
  }
  return 0;
}
