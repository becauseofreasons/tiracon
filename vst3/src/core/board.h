// Tiracon 6V DLP board model - C++ port of emulator/js/tiracon.js.
// Memory: 0000-1FFF ROM, 2000-27FF RAM (battery backed), A14/A15 not decoded.
// I/O (linear select, active low): A2 PIO129, A3 DART, A5 PIO130, A6 PIO122, A7 CTC.
#pragma once
#include "z80.h"
#include "devices.h"
#include <cstdint>
#include <cstring>

namespace t6v {

constexpr int CPU_HZ = 4000000;

// the 28 sample & hold channels of the ALP
enum Cv : int {
  CV_EGA1, CV_EGA2, CV_EGA3, CV_EGA4, CV_EGA5, CV_EGA6,
  CV_EGF1, CV_EGF2, CV_EGF3, CV_EGF4, CV_EGF5, CV_EGF6,
  CV_F1, CV_F2, CV_F3, CV_F4, CV_F5, CV_F6,
  CV_NI, CV_OI, CV_LI1, CV_LI2, CV_LI3, CV_FH, CV_LFO1, CV_LFO2, CV_LFO3, CV_Q,
  CV_COUNT, CV_NONE = -1
};
const char* cvName(int cv);

// component tolerances that the board needs (pitch path), see device.h
struct BoardProfile {
  bool ideal = true;
  double bitErr[12] = {};
  double rangeRatio = 8;
  double refGain = 1;
  struct Voice { double scale = 1, offsetEff = 0, resetUs = 0; } voices[6];
};

// layout of the control voltage frame for the sound model (= T6VSynth.F)
constexpr int FRAME_LEN = 33;

class Board {
public:
  Board();
  Board(const Board&) = delete;
  Board& operator=(const Board&) = delete;

  int64_t now = 0;                          // T-states since start
  uint8_t rom[0x2000];
  uint8_t ram[0x800];
  bool ramDirty = false;
  Z80 cpu;
  CTC ctc;
  DART dart;
  PIO pio122, pio129, pio130;
  IntSource* intChain[5];

  struct Lines { int z = 0, z4 = 0, sp = 1, sr = 1, sb = 0, sm1 = 1, sm2 = 1, seg = 0, dacHi = 0, dacLo = 0; } lines;
  struct Wave { int ssz = 0, sre = 0, spw = 0, sf2 = 0, sf4 = 0; } wave;
  struct CvVal { bool has = false; int dac = 0, sb = 0, ref = 0x80; int64_t t = 0; } cv[CV_COUNT];
  int ref = 0x80;
  int tapeOut = 0, mbi = 1;
  uint32_t blpDown = 0;                     // bit = BLP key code 0..31
  uint8_t blpMatrix[8] = {};
  uint8_t manualDown[64] = {};
  double dispAcc[64] = {};
  int64_t dispLast = 0, dispFrameStart = 0, lastZChange = 0;
  double kalHz = 0, kalPhase = 0, cliHz = 0, cliPhase = 0;
  int kalVoice = -1;
  int64_t kalCheck = 0;
  int64_t idleCycles = 0;
  uint32_t ioCount = 0, warnCount = 0;
  bool hasDevice = false;
  BoardProfile device;

  // MIDI out of the firmware (DART ch.B), optional
  void (*onMidiOut)(void*, uint8_t) = nullptr;
  void* onMidiOutCtx = nullptr;

  void loadRom(const uint8_t* bytes, int n);
  void reset();
  void nmi() { cpu.nmi(); }
  void run(int64_t cycles);                 // like tiracon.js run(): at least `cycles` T-states
  void runUntil(int64_t t) { if (t > now) run(t - now); }

  // inputs
  void setBlpKey(int code, bool down);
  void setManualKey(int e, bool down) { if (e >= 0 && e < 49) manualDown[e] = down ? 1 : 0; }
  void midiIn(const uint8_t* bytes, int n) { dart.receive(bytes, n); }
  void setCliHz(double hz) { cliHz = hz > 0 ? hz : 0; }
  void setDevice(const BoardProfile* p) { hasDevice = p && !p->ideal; if (hasDevice) device = *p; }

  // outputs
  double cvLevel(int cv) const;
  double vcoHz(int v) const;
  void audioFrame(float* a) const;          // FRAME_LEN values
  void takeDisplay(float* out64);           // brightness 0..1, 8 digits x 8 segments, resets
  int voiceKey(int v) const { int k = ram[0x7C + v]; return k == 0x63 ? -1 : k; }

  // bus interface for the CPU
  int read(int addr) const {
    int a = addr & 0x3FFF;
    if (a < 0x2000) return rom[a];
    if (a < 0x2800) return ram[a - 0x2000];
    return 0xFF;
  }
  void write(int addr, int v) {
    int a = addr & 0x3FFF;
    if (a >= 0x2000 && a < 0x2800) { ram[a - 0x2000] = uint8_t(v); ramDirty = true; return; }
    if (a < 0x2000) warnCount++;            // write to ROM
  }
  int ioRead(int port);
  void ioWrite(int port, int v);
  bool intRequested() const;
  uint8_t intAck();
  void reti();

  // PIO wiring
  int pioInput(int id, int port);
  void pioOutput(int id, int port, int out, int drive);

private:
  uint8_t ioSel[256];                       // bit 0 PIO129, 1 DART, 2 PIO130, 3 PIO122, 4 CTC
  int dac() const { return (lines.dacHi << 4) | lines.dacLo; }
  double dacReal(int d) const;
  void trackRef() { if (lines.sr == 0) ref = lines.dacHi; }
  void trackSH();
  void updateKal();
  void flushDisplay();
};

} // namespace t6v
