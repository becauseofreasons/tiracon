#include "engine.h"
#include "uimodel.h"
#include <algorithm>
#include <cmath>

namespace t6v {

namespace {
bool isProgram(int n) { int b = n / 10, s = n % 10; return b >= 1 && b <= 4 && s >= 1 && s <= 8; }
}

const RomInfo& romInfo() {
  static const RomInfo info = parseRom(FIRMWARE_ROM, sizeof FIRMWARE_ROM);
  return info;
}

const std::vector<ParamDef>& firmwareParams() {
  static const std::vector<ParamDef> list = [] {
    std::vector<ParamDef> v;
    const RomInfo& r = romInfo();
    for (const ParamInfo& p : allParams())
      if (p.no >= 11 && p.no <= 76 && r.cc) v.push_back({ p.no, p.max, &p });
    return v;
  }();
  return list;
}

Engine::Engine() : board(std::make_unique<Board>()) {
  board->loadRom(FIRMWARE_ROM, sizeof FIRMWARE_ROM);
  std::memcpy(board->ram, factoryRam(), 0x800);
  device = makeDevice("1266");
  hasDevice = true;
  board->setDevice(&device.board);
  board->reset();
  syncFromRam();
}

void Engine::prepare(double sampleRate) {
  fs = sampleRate > 0 ? sampleRate : 48000;
  synth = std::make_unique<Synth>(fs);
  if (hasDevice) synth->setDevice(&device.synth);
  synth->master = volume;
  cycAcc = double(board->now);
}

void Engine::powerOn() {
  board->reset();
  restoreStage = 0;
  syncFromRam();
}
void Engine::loadRam(const uint8_t* ram2k) {
  std::memcpy(board->ram, ram2k, 0x800);
  restoreStage = 0;                                // new RAM: restore from this one
  powerOnRestoring();
}
// remember program and edit buffer, power on, bring them back once the firmware is up (see service())
void Engine::powerOnRestoring() {
  if (restoreStage == 0) {                         // else keep the values of the restore still on its way
    const RomInfo& r = romInfo();
    std::fill(std::begin(restoreVal), std::end(restoreVal), -1);
    for (const ParamDef& p : firmwareParams()) restoreVal[p.no] = ramValue(r, board->ram, p.no);
    int pn = bcd(board->ram[RAM_PROGNO]);
    restoreProg = isProgram(pn) ? (pn / 10 - 1) * 8 + pn % 10 - 1 : -1;
  }
  powerOn();
  restoreStage = 1;
  restoreAt = board->now + RESTORE_BOOT;
}
void Engine::setDevice(const DeviceProfile& d) {
  device = d;
  hasDevice = true;
  board->setDevice(&device.board);
  if (synth) synth->setDevice(&device.synth);
  powerOnRestoring();
}

// parameter values as the host should see them = edit buffer in the RAM
void Engine::syncFromRam() {
  const RomInfo& r = romInfo();
  for (const ParamDef& p : firmwareParams()) {
    int v = ramValue(r, board->ram, p.no);
    ParamState& s = ps[p.no];
    s.want = s.sent = v; s.fwLast = -1; s.pending = false; s.lastAt = INT64_MIN / 2;
  }
  int pn = bcd(board->ram[RAM_PROGNO]);
  progWant = isProgram(pn) ? (pn / 10 - 1) * 8 + pn % 10 - 1 : -1;
  progFw = -1; progPending = -1;
}

// MIDI channel the firmware listens to (P82: 0 = omni -> channel 1)
int Engine::channel() const {
  int ch = ramValue(romInfo(), board->ram, 82);
  return ch >= 1 && ch <= 16 ? ch - 1 : 0;
}

void Engine::noteOn(int pitch, float velocity) {
  int v = int(std::lround(velocity * 127));
  v = std::max(1, std::min(127, v));
  uint8_t b[3] = { uint8_t(0x90 | channel()), uint8_t(pitch & 0x7F), uint8_t(v) };
  send(b, 3);
}
void Engine::noteOff(int pitch) {
  uint8_t b[3] = { uint8_t(0x80 | channel()), uint8_t(pitch & 0x7F), 0x40 };
  send(b, 3);
}

void Engine::sendParam(int no, int v) {
  ParamState& s = ps[no];
  uint8_t b[3] = { uint8_t(0xB0 | channel()), uint8_t(no), uint8_t(ccValue(romInfo(), no, v)) };
  send(b, 3);
  s.sent = v; s.lastAt = board->now; s.pending = false;
}
void Engine::hostParam(int no, int value) {
  if (no < 0 || no >= 100 || !romInfo().has(no) || no == 88) return;
  ParamState& s = ps[no];
  s.want = value;
  if (value == s.sent) { s.pending = false; return; }       // same value as last sent: nothing to do
  if (board->now - s.lastAt >= CC_WINDOW) sendParam(no, value);
  else s.pending = true;
}

void Engine::sendPitchBend(int v) {
  uint8_t b[3] = { uint8_t(0xE0 | channel()), uint8_t(v & 0x7F), uint8_t((v >> 7) & 0x7F) };
  send(b, 3);
  pbSent = v; pbLastAt = board->now; pbPending = false;
}
void Engine::hostPitchBend(double normalized) {
  int v = int(std::lround(std::max(0.0, std::min(1.0, normalized)) * 16383));
  pbWant = v;
  if (v == pbSent) { pbPending = false; return; }
  if (board->now - pbLastAt >= PB_WINDOW) sendPitchBend(v);
  else pbPending = true;
}

// The firmware takes MIDI program changes only with P84 PROGR-CHG = 1: switch it on by CC first
// and send the program change a little later (the CC is applied in the main loop).
void Engine::hostProgram(int index) {
  if (index < 0 || index >= PROGRAMS) return;
  progWant = index;
  progPending = index;
  progAt = board->now;
  if (ramValue(romInfo(), board->ram, 84) == 0) {
    uint8_t b[3] = { uint8_t(0xB0 | channel()), 84, uint8_t(ccValue(romInfo(), 84, 1)) };
    send(b, 3);
    progAt = board->now + CPU_HZ * 30 / 1000;
  }
}

void Engine::service() {
  int64_t now = board->now;
  for (int no = 11; no < 100; no++) {
    ParamState& s = ps[no];
    if (s.pending && now - s.lastAt >= CC_WINDOW) {
      if (s.want != s.sent) sendParam(no, s.want); else s.pending = false;
    }
  }
  if (pbPending && now - pbLastAt >= PB_WINDOW) sendPitchBend(pbWant);
  if (progPending >= 0 && now >= progAt) {
    uint8_t b[2] = { uint8_t(0xC0 | channel()), uint8_t(progPending) };
    send(b, 2);
    progPending = -1;
    progAt = now;
  }
  // after a power on: program change, then the edit buffer values that differ from the stored program
  if (restoreStage == 1 && now >= restoreAt) {
    if (restoreProg >= 0) hostProgram(restoreProg);
    restoreStage = 2;
    restoreAt = std::max(now, progAt) + RESTORE_EDIT;
  } else if (restoreStage == 2 && now >= restoreAt) {
    const RomInfo& r = romInfo();
    for (const ParamDef& p : firmwareParams()) {
      int v = restoreVal[p.no];
      if (v >= 0 && v <= p.max && ramValue(r, board->ram, p.no) != v) resendParam(p.no, v);
    }
    restoreStage = 0;
  }
  // feedback: edit buffer and program number -> host (not while booting into program 11 before the restore)
  if (now >= nextPoll && restoreStage == 0) {
    nextPoll = now + POLL;
    const RomInfo& r = romInfo();
    for (const ParamDef& p : firmwareParams()) {
      ParamState& s = ps[p.no];
      int fw = ramValue(r, board->ram, p.no);
      if (fw < 0 || fw > p.max || fw == s.fwLast) continue;
      s.fwLast = fw;
      if (s.pending || now - s.lastAt < ECHO_HOLD) continue;   // our own change is still on its way
      if (fw != s.want) { s.want = s.sent = fw; pushChange(p.no, fw); }
    }
    int pn = bcd(board->ram[RAM_PROGNO]);
    if (isProgram(pn)) {
      int idx = (pn / 10 - 1) * 8 + pn % 10 - 1;
      if (idx != progFw) {
        progFw = idx;
        if (progPending < 0 && now - progAt >= ECHO_HOLD && idx != progWant) { progWant = idx; pushChange(-1, idx); }
      }
    }
  }
}

void Engine::render(float* out, int n) {
  if (!synth) { std::fill(out, out + n, 0.0f); return; }
  const double cps = double(CPU_HZ) / fs;
  for (int pos = 0; pos < n; ) {
    int len = std::min(32, n - pos);
    service();
    cycAcc += len * cps;
    board->runUntil(int64_t(cycAcc));
    board->audioFrame(frame);
    synth->setFrame(frame);
    synth->process(out + pos, len);
    pos += len;
  }
}

int Engine::takeChanges(Change* out, int max) {
  int n = std::min(max, nChanges);
  std::copy(changes, changes + n, out);
  nChanges = 0;
  return n;
}

} // namespace t6v
