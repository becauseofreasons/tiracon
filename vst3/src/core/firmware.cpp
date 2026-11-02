#include "firmware.h"

namespace t6v {

namespace {
#include "gen/factory_ram.inc"
bool okBcd(int v) { return (v >> 4) < 10 && (v & 15) < 10; }
}

const uint8_t* factoryRam() { return FACTORY_RAM; }

// same rules as parseRom() in emulator/js/params.js
RomInfo parseRom(const uint8_t* rom, int len) {
  RomInfo info;
  for (int i = 0; i < 100; i++) { info.max[i] = -1; info.pv[i] = -1; }
  if (!rom || len < 0x2000) return info;
  for (int i = 0; i < 45; i++) {                  // PARAM_TAB 09D3h: db number, max (BCD) / dw handler
    int a = 0x09D3 + 4 * i;
    if (okBcd(rom[a]) && okBcd(rom[a + 1]) && bcd(rom[a]) >= 11) { info.max[bcd(rom[a])] = bcd(rom[a + 1]); info.pv[bcd(rom[a])] = 0xCF + i; }
  }
  if (rom[0x620] == 0xC3) {                       // mod: jp PARAM_EXT / ld hl,EXT_TAB / ld d,count
    int px = rom[0x621] | rom[0x622] << 8;
    if (px < 0x1FF0 && rom[px] == 0x21 && rom[px + 3] == 0x16) {
      int tab = rom[px + 1] | rom[px + 2] << 8, n = rom[px + 4];
      for (int k = 0; k < n && tab + 4 * k + 1 < 0x2000; k++) {
        int b = tab + 4 * k;
        if (okBcd(rom[b]) && okBcd(rom[b + 1])) { info.max[bcd(rom[b])] = bcd(rom[b + 1]); info.pv[bcd(rom[b])] = 0x408 + k; }
      }
      info.ext = true;
    }
  }
  // MIDI-CC builds hook the end of the main loop (original: jp MAIN_CHK_SERVICE 1970h)
  info.cc = rom[0x1AB] == 0xC3 && (rom[0x1AC] | rom[0x1AD] << 8) != 0x1970;
  info.seqRemoved = info.max[88] == 1;
  return info;
}

} // namespace t6v
