#include "multiengine.h"
#include "uimodel.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace t6v {

namespace {
// RAM that WRITE and MAST. TUNE change: 2187h-2407h (MAST. TUNE + 32 programs), 2460h-25C3h (mod extension table)
constexpr int R1A = 0x187, R1B = 0x408, R2A = 0x460, R2B = 0x5C4;
int unisonSize(int p37) { static const int S[5] = { 1, 2, 3, 4, 6 }; return p37 >= 0 && p37 <= 4 ? S[p37] : 1; }
}

std::array<DeviceProfile, MultiEngine::MAX> MultiEngine::makeDevices(const std::string& base) {
  std::array<DeviceProfile, MAX> d;
  for (int i = 0; i < MAX; i++) d[size_t(i)] = makeDevice(serialOf(base, i));
  return d;
}

MultiEngine::MultiEngine() {
  for (int i = 0; i < MAX; i++) {
    e[size_t(i)] = std::make_unique<Engine>();
    if (i > 0) e[size_t(i)]->setDevice(makeDevice(serialOf("1266", i)));
  }
  std::fill(std::begin(noteInst), std::end(noteInst), int8_t(-1));
}

void MultiEngine::prepare(double sampleRate, int maxBlock) {
  for (auto& x : e) { x->prepare(sampleRate); x->setVolume(volume); }
  scratch.assign(size_t(std::max(maxBlock, 32)), 0.0f);
}

void MultiEngine::releaseAll() {
  for (int p = 0; p < 128; p++)
    if (noteInst[p] >= 0) { if (noteInst[p] < n) e[size_t(noteInst[p])]->noteOff(p); noteInst[p] = -1; }
  std::fill(std::begin(held), std::end(held), 0);
}

void MultiEngine::setCount(int c) {
  int want = c >= 8 ? 8 : c >= 4 ? 4 : c >= 2 ? 2 : 1;
  if (want == n) return;
  releaseAll();
  // new instances start like a unit that is switched on: same RAM as instance 1
  for (int i = n; i < want; i++) {
    e[size_t(i)]->loadRam(e[0]->ram());
    e[size_t(i)]->setVolume(volume);
  }
  n = want;
  rr = 0;
  std::memset(diffSeen, 0, sizeof diffSeen);
  std::fill(std::begin(progDiff), std::end(progDiff), false);
}

void MultiEngine::noteOn(int pitch, float velocity) {
  pitch &= 0x7F;
  int inst = noteInst[pitch];
  if (inst < 0 || inst >= n) {
    // capacity of an instance: 6 voices / unison group size (instance 1 decides, all have the same program)
    int cap = 6 / unisonSize(ramValue(romInfo(), e[0]->ram(), 37));
    int best = -1;
    for (int k = 0; k < n; k++) {
      int i = (rr + k) % n;
      bool free = held[i] < cap;
      if (best < 0) { best = i; continue; }
      bool bestFree = held[best] < cap;
      if ((free && !bestFree) || (free == bestFree && held[i] < held[best])) best = i;
    }
    inst = best;
    rr = (inst + 1) % n;
    noteInst[pitch] = int8_t(inst);
    held[inst]++;
  }
  e[size_t(inst)]->noteOn(pitch, velocity);
}

void MultiEngine::noteOff(int pitch) {
  pitch &= 0x7F;
  int inst = noteInst[pitch];
  if (inst >= 0 && inst < n) {
    e[size_t(inst)]->noteOff(pitch);
    held[inst] = std::max(0, held[inst] - 1);
  } else {
    for (int i = 0; i < n; i++) e[size_t(i)]->noteOff(pitch);    // unknown note: to all
  }
  noteInst[pitch] = -1;
}

// keep instances 2..n equal to instance 1. Panel keys go to all, so normally nothing differs; this
// repairs what got lost (MIDI bytes dropped by an overload, a WRITE done while an instance was busy).
void MultiEngine::sync() {
  const uint8_t* r0 = e[0]->ram();
  const RomInfo& rom = romInfo();
  int pn0 = bcd(r0[RAM_PROGNO]);
  for (int i = 1; i < n; i++) {
    Engine& x = *e[size_t(i)];
    uint8_t* ri = x.hw().ram;
    // stored programs and MAST. TUNE: battery RAM, copied directly (the firmware reads them on program load)
    if (std::memcmp(r0 + R1A, ri + R1A, R1B - R1A) != 0) std::memcpy(ri + R1A, r0 + R1A, R1B - R1A);
    if (std::memcmp(r0 + R2A, ri + R2A, R2B - R2A) != 0) std::memcpy(ri + R2A, r0 + R2A, R2B - R2A);
    // program number, then the edit buffer: only if the difference is still there at the next check
    int pni = bcd(ri[RAM_PROGNO]);
    if (isProgramNo(pn0) && pni != pn0) {
      if (progDiff[i]) { x.hostProgram((pn0 / 10 - 1) * 8 + pn0 % 10 - 1); progDiff[i] = false; }
      else progDiff[i] = true;
      continue;                                      // edit buffer follows after the program change
    }
    progDiff[i] = false;
    for (const ParamInfo& p : allParams()) {
      if (p.no == 88) continue;
      int v0 = ramValue(rom, r0, p.no), vi = ramValue(rom, ri, p.no);
      if (v0 < 0 || v0 > p.max || v0 == vi) { diffSeen[i][p.no] = 0; continue; }
      if (diffSeen[i][p.no]) { x.resendParam(p.no, v0); diffSeen[i][p.no] = 0; }
      else diffSeen[i][p.no] = 1;
    }
  }
}

void MultiEngine::render(float* out, int samples) {
  if (n == 1) { e[0]->render(out, samples); return; }      // exactly the single unit, nothing added
  if (int(scratch.size()) < samples) samples = int(scratch.size());
  e[0]->render(out, samples);
  for (int i = 1; i < n; i++) {
    e[size_t(i)]->render(scratch.data(), samples);
    for (int k = 0; k < samples; k++) out[k] += scratch[size_t(k)];
  }
  // several units summed: about the loudness of one for a single note, soft limit like the ALP output
  double g = 1.0 / std::sqrt(double(n));
  for (int k = 0; k < samples; k++) out[k] = float(std::tanh(out[k] * g));
  // the other instances' feedback is not needed (instance 1 speaks for all)
  Engine::Change dummy[128];
  for (int i = 1; i < n; i++) e[size_t(i)]->takeChanges(dummy, 128);
  if (e[0]->hw().now >= nextSync) { nextSync = e[0]->hw().now + SYNC; sync(); }
}

int MultiEngine::takeChanges(Engine::Change* out, int max) { return e[0]->takeChanges(out, max); }

void MultiEngine::loadRam(const uint8_t* ram2k) {
  std::fill(std::begin(noteInst), std::end(noteInst), int8_t(-1));
  std::fill(std::begin(held), std::end(held), 0);
  for (auto& x : e) x->loadRam(ram2k);
}
void MultiEngine::setDevices(const std::array<DeviceProfile, MAX>& d) {
  std::fill(std::begin(noteInst), std::end(noteInst), int8_t(-1));
  std::fill(std::begin(held), std::end(held), 0);
  for (int i = 0; i < MAX; i++) e[size_t(i)]->setDevice(d[size_t(i)]);
}
void MultiEngine::powerOn() {
  std::fill(std::begin(noteInst), std::end(noteInst), int8_t(-1));
  std::fill(std::begin(held), std::end(held), 0);
  for (auto& x : e) x->powerOn();
}

} // namespace t6v
