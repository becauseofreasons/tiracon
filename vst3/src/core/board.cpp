// Tiracon 6V DLP board model - C++ port of emulator/js/tiracon.js. Keep both in step.
#include "board.h"
#include <cmath>
#include <algorithm>

namespace t6v {

namespace {
const char* CV_NAMES[CV_COUNT] = {
  "EGA1", "EGA2", "EGA3", "EGA4", "EGA5", "EGA6", "EGF1", "EGF2", "EGF3", "EGF4", "EGF5", "EGF6",
  "F1", "F2", "F3", "F4", "F5", "F6", "NI", "OI", "LI1", "LI2", "LI3", "FH", "LFO1", "LFO2", "LFO3", "Q" };

// ALP demultiplexer outputs (from the ALP netlist)
const int DM1[8] = { CV_EGA4, CV_EGA3, CV_EGA2, CV_EGA1, CV_NI, CV_OI, CV_LI2, CV_LI3 };
const int DM2[8] = { CV_F6, CV_F2, CV_F4, CV_EGA6, CV_F5, CV_F1, CV_F3, CV_EGA5 };
const int DM3[8] = { CV_EGF6, CV_EGF2, CV_EGF4, CV_LI1, CV_EGF5, CV_EGF1, CV_EGF3, CV_FH };
const int DM4[8] = { CV_LFO3, CV_LFO1, CV_Q, CV_LFO2, CV_NONE, CV_NONE, CV_NONE, CV_NONE };

int demuxChannel(int z, int z4) {          // z = Z0..Z3 (phase), z4 = 0/1
  int b0 = z & 1, b1 = (z >> 1) & 1, b2 = (z >> 2) & 1, z3 = (z >> 3) & 1, idx;
  if (z3 == 0) {                           // D201 / D203: S0..S2 = /Z0../Z2
    idx = ((b2 ^ 1) << 2) | ((b1 ^ 1) << 1) | (b0 ^ 1);
    return (z4 ? DM4 : DM1)[idx];
  }
  idx = ((b0 ^ 1) << 2) | ((b1 ^ 1) << 1) | (b2 ^ 1);   // D202 / D204: S0 and S2 swapped
  return (z4 ? DM3 : DM2)[idx];
}

// BLP key code -> matrix position (column = Z0..Z2, row = PIO122 PB0..3)
void blpPos(int code, int& col, int& row) {
  int k = code >> 3, bit = code & 7;
  if (bit >= 4) { col = 2 * k; row = bit - 4; } else { col = 2 * k + 1; row = bit; }
}

// VCO model, calibrated from the firmware's own autotune targets (see tiracon.js)
constexpr double VCO_HZ_PER_UNIT = 440.0 / 3250.0;
constexpr double REF_BETA = 1.0 / 1669.0;

enum { SEL_PIO129 = 1, SEL_DART = 2, SEL_PIO130 = 4, SEL_PIO122 = 8, SEL_CTC = 16 };
} // namespace

const char* cvName(int cv) { return cv >= 0 && cv < CV_COUNT ? CV_NAMES[cv] : ""; }

Board::Board()
  : cpu(*this), dart(&now), pio122(0, this), pio129(1, this), pio130(2, this) {
  std::memset(rom, 0xFF, sizeof rom);
  std::memset(ram, 0, sizeof ram);
  intChain[0] = &dart.srcB;
  for (int n = 0; n < 4; n++) intChain[1 + n] = &ctc.ch[n].src;
  dart.onTx = [](void* ctx, uint8_t b) {
    Board* self = static_cast<Board*>(ctx);
    if (self->onMidiOut) self->onMidiOut(self->onMidiOutCtx, b);
  };
  dart.onTxCtx = this;
  for (int p = 0; p < 256; p++) {
    int s = 0;
    if (!(p & 0x04)) s |= SEL_PIO129;
    if (!(p & 0x08)) s |= SEL_DART;
    if (!(p & 0x20)) s |= SEL_PIO130;
    if (!(p & 0x40)) s |= SEL_PIO122;
    if (!(p & 0x80)) s |= SEL_CTC;
    ioSel[p] = uint8_t(s);
  }
}

void Board::loadRom(const uint8_t* bytes, int n) {
  std::memset(rom, 0xFF, sizeof rom);
  std::memcpy(rom, bytes, size_t(std::min(n, 0x2000)));
}

// ---------------------------------------------------------------- I/O
int Board::ioRead(int port) {
  int lo = port & 0xFF, sel = ioSel[lo];
  ioCount++;
  if (!sel) return 0xFF;
  if (sel & (sel - 1)) warnCount++;        // several chips selected
  int v = 0xFF;
  if (sel & SEL_PIO129) v &= pio129.read(lo & 3);
  if (sel & SEL_DART) v &= dart.read(lo & 3);
  if (sel & SEL_PIO130) v &= pio130.read(lo & 3);
  if (sel & SEL_PIO122) v &= pio122.read(lo & 3);
  if (sel & SEL_CTC) v &= ctc.read(lo & 3);
  return v;
}
void Board::ioWrite(int port, int v) {
  int lo = port & 0xFF, sel = ioSel[lo];
  ioCount++;
  if (!sel) { warnCount++; return; }
  if (sel & (sel - 1)) warnCount++;
  if (sel & SEL_PIO129) pio129.write(lo & 3, v);
  if (sel & SEL_DART) dart.write(lo & 3, v);
  if (sel & SEL_PIO130) pio130.write(lo & 3, v);
  if (sel & SEL_PIO122) pio122.write(lo & 3, v);
  if (sel & SEL_CTC) ctc.write(lo & 3, v);
}

// ---------------------------------------------------------------- interrupts (daisy chain)
bool Board::intRequested() const {
  for (IntSource* s : intChain) {
    if (s->ius) return false;
    if (s->pending) return true;
  }
  return false;
}
uint8_t Board::intAck() {
  for (IntSource* s : intChain) {
    if (s->ius) break;
    if (s->pending) return s->ack();
  }
  warnCount++;                             // spurious interrupt
  return 0xFF;
}
void Board::reti() {
  for (IntSource* s : intChain) if (s->ius) { s->ius = false; return; }
}

// ---------------------------------------------------------------- PIO wiring
int Board::pioInput(int id, int port) {
  if (id == 0 && port == 0) {              // PIO122 A: PA0-3 = manual groups (low active)
    int row = lines.z, v = 0xF0;
    for (int g = 0; g < 4; g++) if (!manualDown[row + 16 * g]) v |= 1 << g;
    return v;
  }
  if (id == 0 && port == 1) return blpMatrix[lines.z & 7] & 0x0F;   // PIO122 B: BLP rows
  if (id == 2 && port == 0) return mbi ? 0xFF : 0x7F;               // PIO130 A: MBI
  return 0xFF;
}
void Board::pioOutput(int id, int port, int out, int drive) {
  Lines& L = lines;
  if (id == 0 && port == 0) {              // PIO122 A: PA4-7 = Z0..Z3
    int z = (drive & 0xF0) ? (out >> 4) & 0x0F : L.z;
    if (z != L.z) { flushDisplay(); L.z = z; lastZChange = now; trackSH(); }
  } else if (id == 0 && port == 1) {       // PIO122 B: PB4-7 = DAC bit 12..9
    L.dacLo = (out >> 4) & 0x0F; trackSH();
  } else if (id == 1 && port == 0) {       // PIO129 A: segments
    int seg = drive ? out & drive : 0;
    if (seg != L.seg) { flushDisplay(); L.seg = seg; }
  } else if (id == 1 && port == 1) {       // PIO129 B: DAC bit 1..8
    L.dacHi = out; trackSH();
  } else if (id == 2 && port == 0) {       // PIO130 A: Z4 SP - - TAPE SRE SF2 MBI(in)
    int z4 = out & 1, sp = (drive & 2) ? (out >> 1) & 1 : 1;
    if (z4 != L.z4 || sp != L.sp) { L.z4 = z4; L.sp = sp; trackSH(); }
    tapeOut = (out >> 4) & 1;
    wave.sre = (out >> 5) & 1;
    wave.sf2 = (out >> 6) & 1;
  } else if (id == 2 && port == 1) {       // PIO130 B: SM1 SM2 SR SB - SPW SF4 SSZ
    L.sm1 = out & 1; L.sm2 = (out >> 1) & 1;
    int sr = (out >> 2) & 1, sb = (out >> 3) & 1;
    if (sr != L.sr) { L.sr = sr; trackRef(); }
    if (sb != L.sb) { L.sb = sb; trackSH(); }
    wave.spw = (out >> 5) & 1;
    wave.sf4 = (out >> 6) & 1;
    wave.ssz = (out >> 7) & 1;
  }
}

// demux S&H: the addressed capacitor follows the DAC while SP = 0
void Board::trackSH() {
  Lines& L = lines;
  if (L.sr == 0) ref = L.dacHi;
  if (L.sp != 0) return;
  int ch = demuxChannel(L.z, L.z4);
  if (ch < 0) return;
  CvVal& c = cv[ch];
  if (!c.has) { c.has = true; c.dac = 0; c.sb = 0; c.ref = 0x80; c.t = 0; }
  c.dac = dac(); c.sb = L.sb; c.t = now;
  if (cvName(ch)[0] == 'F') c.ref = ref;     // F1..F6 (and FH, like the JS version)
}

double Board::cvLevel(int ch) const {
  const CvVal& c = cv[ch];
  if (!c.has) return 0;
  return (c.sb ? c.dac / 8.0 : double(c.dac)) / 4095.0;
}

double Board::dacReal(int d) const {
  if (!hasDevice) return d;
  double x = d;
  for (int k = 0; k < 12; k++) if (d & (1 << k)) x += device.bitErr[k];
  return x;
}
double Board::vcoHz(int v) const {
  const CvVal& c = cv[CV_F1 + v];
  if (!c.has) return 0;
  if (!hasDevice) {
    double eff = c.sb ? double(c.dac) : double(c.dac) * 8;
    return VCO_HZ_PER_UNIT * eff * (1 - (c.ref - 0x80) * REF_BETA);
  }
  const BoardProfile::Voice& t = device.voices[v];
  double d = dacReal(c.dac);
  double effT = c.sb ? d * 8 / device.rangeRatio : d * 8;
  double f0 = VCO_HZ_PER_UNIT * t.scale * (effT + t.offsetEff) * (1 - (c.ref - 0x80) * REF_BETA * device.refGain);
  if (f0 <= 0) return 0;
  return f0 / (1 + f0 * t.resetUs * 1e-6);
}
// KAL: trigger output of the summed voices -> loudest voice, divided by the lowest footage
void Board::updateKal() {
  int best = -1;
  double lvl = 0.02;
  for (int v = 0; v < 6; v++) {
    double l = cvLevel(CV_EGA1 + v);
    if (l > lvl) { lvl = l; best = v; }
  }
  int div = wave.sf4 ? 4 : (wave.sf2 ? 2 : 1);
  kalVoice = best;
  kalHz = best >= 0 ? vcoHz(best) / div : 0;
}

void Board::audioFrame(float* a) const {
  for (int v = 0; v < 6; v++) {
    a[v] = float(vcoHz(v));
    a[6 + v] = float(cvLevel(CV_EGA1 + v));
    a[12 + v] = float(cvLevel(CV_EGF1 + v));
  }
  static const int NAMES[10] = { CV_Q, CV_FH, CV_OI, CV_NI, CV_LFO1, CV_LFO2, CV_LFO3, CV_LI1, CV_LI2, CV_LI3 };
  for (int v = 0; v < 10; v++) a[18 + v] = float(cvLevel(NAMES[v]));
  a[28] = float(wave.ssz); a[29] = float(wave.sre); a[30] = float(wave.spw); a[31] = float(wave.sf2); a[32] = float(wave.sf4);
}

// ---------------------------------------------------------------- display
void Board::flushDisplay() {
  int64_t dt = now - dispLast;
  dispLast = now;
  int seg = lines.seg;
  if (!seg || dt <= 0) return;
  if (now - lastZChange > 40000) return;   // BLP blanks the display without multiplex clock
  int base = (lines.z & 7) * 8;
  for (int b = 0; b < 8; b++) if (seg & (1 << b)) dispAcc[base + b] += double(dt);
}
void Board::takeDisplay(float* out) {
  flushDisplay();
  int64_t span = now - dispFrameStart;
  for (int i = 0; i < 64; i++) out[i] = span > 0 ? float(std::min(1.0, dispAcc[i] / double(span) * 8)) : 0.0f;
  std::memset(dispAcc, 0, sizeof dispAcc);
  dispFrameStart = now;
}

// ---------------------------------------------------------------- inputs / control
void Board::setBlpKey(int code, bool down) {
  if (code < 0 || code > 31) return;
  if (down) blpDown |= 1u << code; else blpDown &= ~(1u << code);
  std::memset(blpMatrix, 0, sizeof blpMatrix);
  for (int k = 0; k < 32; k++) if (blpDown & (1u << k)) {
    int col, row; blpPos(k, col, row);
    blpMatrix[col] |= uint8_t(1 << row);
  }
}

void Board::reset() {
  cpu.reset();
  ctc.reset();
  dart.reset();
  dart.clearRx();
  pio122.reset(); pio129.reset(); pio130.reset();
  lines.sp = 1; lines.sr = 1;
}

void Board::run(int64_t cycles) {
  int64_t end = now + cycles;
  while (now < end) {
    int t = cpu.step();
    now += t;
    // CPU time outside any interrupt service routine (= main program)
    if (!(intChain[0]->ius || intChain[1]->ius || intChain[2]->ius || intChain[3]->ius || intChain[4]->ius)) idleCycles += t;
    ctc.tick(t);
    dart.tick();
    kalCheck += t;
    if (kalCheck >= 2000) { kalCheck = 0; updateKal(); }
    if (kalHz > 0) {
      kalPhase += t * kalHz / CPU_HZ;
      if (kalPhase >= 1) { kalPhase -= std::floor(kalPhase); ctc.trigger(2); }
    }
    if (cliHz > 0) {
      cliPhase += t * cliHz / CPU_HZ;
      if (cliPhase >= 1) { cliPhase -= std::floor(cliPhase); ctc.trigger(0); }
    }
  }
}

} // namespace t6v
