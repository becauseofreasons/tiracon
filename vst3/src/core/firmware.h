// Firmware helpers: built-in ROM, parameter tables read from the ROM (port of parseRom in
// emulator/js/params.js), factory RAM, MIDI-CC value mapping.
#pragma once
#include <cstdint>

namespace t6v {

// built-in firmware (firmware/tiracon_mod.bin, embedded by CMake)
extern const unsigned char FIRMWARE_ROM[8192];
const uint8_t* factoryRam();                      // 2 KB: 32 factory programs, MAST. TUNE 32

struct RomInfo {
  int max[100];          // parameter number -> max value, -1 = not in this firmware
  int pv[100];           // parameter number -> RAM offset (from 2000h) of its edit buffer byte (BCD), -1
  bool ext = false, cc = false, seqRemoved = false;
  bool has(int no) const { return no >= 0 && no < 100 && max[no] >= 0; }
};
RomInfo parseRom(const uint8_t* rom, int len);
const RomInfo& romInfo();                         // tables of the built-in firmware

inline int bcd(int v) { return (v >> 4) * 10 + (v & 15); }
// current value of a parameter in a RAM image, -1 if unknown
inline int ramValue(const RomInfo& r, const uint8_t* ram, int no) {
  return r.has(no) && r.pv[no] >= 0 ? bcd(ram[r.pv[no]]) : -1;
}
// CC value for a parameter value: the firmware maps CC v to v * (max + 1) / 128, use the middle of the step
inline int ccValue(const RomInfo& r, int no, int v) {
  int max = r.max[no];
  int x = int((v + 0.5) * 128 / (max + 1));
  return x < 0 ? 0 : (x > 127 ? 127 : x);
}

// RAM addresses (offsets from 2000h)
constexpr int RAM_PROGNO = 0xFC;      // current program (BCD 11..48)
constexpr int RAM_PROGMODE = 0xCE;
constexpr int RAM_MASTER_TUNE = 0x187;
constexpr int RAM_PROGRAMS = 0x188;   // 32 x 20 bytes

} // namespace t6v
