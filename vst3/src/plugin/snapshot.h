// Fills the editor snapshot from the engine (audio thread; no allocation)
#pragma once
#include "bridge.h"
#include "multiengine.h"
#include <cstring>
#include <string>

namespace tiracon {

inline void fillSnapshot(t6v::MultiEngine& m, UiSnapshot& s, const std::string& serial) {
  t6v::Engine& e = m.primary();
  t6v::Board& b = e.hw();
  s.time = double(b.now) / t6v::CPU_HZ;
  b.takeDisplay(s.disp);
  std::memcpy(s.ram, b.ram, sizeof s.ram);
  s.instances = m.count();
  for (int i = 0; i < t6v::MultiEngine::MAX; i++)
    for (int v = 0; v < 6; v++) {
      UiSnapshot::Voice& vo = s.voices[i * 6 + v];
      if (i >= m.count()) { vo = UiSnapshot::Voice(); continue; }
      t6v::Board& bi = m.at(i).hw();
      vo.key = bi.voiceKey(v);
      vo.state = bi.ram[0x58 + v];
      vo.hz = float(bi.vcoHz(v));
      vo.ega = float(bi.cvLevel(t6v::CV_EGA1 + v));
      vo.egf = float(bi.cvLevel(t6v::CV_EGF1 + v));
    }
  for (int c = 0; c < t6v::CV_COUNT; c++) { s.cv[c] = float(b.cvLevel(c)); s.cvHas[c] = b.cv[c].has; }
  s.wave[0] = uint8_t(b.wave.ssz); s.wave[1] = uint8_t(b.wave.sre); s.wave[2] = uint8_t(b.wave.spw);
  s.wave[3] = uint8_t(b.wave.sf2); s.wave[4] = uint8_t(b.wave.sf4);
  s.peak = float(e.takePeak());
  std::strncpy(s.serial, serial.c_str(), sizeof s.serial - 1);
  s.serial[sizeof s.serial - 1] = 0;
}

} // namespace tiracon
