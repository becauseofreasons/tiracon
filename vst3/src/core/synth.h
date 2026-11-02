// Tiracon 6V sound generation - C++ port of emulator/js/synth-worklet.js (ideal model of KLP x6 + ALP).
// Input: control voltage frame from the board (Board::audioFrame), output: mono samples.
#pragma once
#include <cstdint>

namespace t6v {

namespace F {
enum : int { HZ = 0, EGA = 6, EGF = 12, Q = 18, FH = 19, OI = 20, NI = 21, LFO1 = 22, LFO2 = 23, LFO3 = 24,
             LI1 = 25, LI2 = 26, LI3 = 27, SSZ = 28, SRE = 29, SPW = 30, SF2 = 31, SF4 = 32, LEN = 33 };
}

// component values per voice / of the ALP (device.h fills them for a real unit)
struct SynthVoiceProfile {
  double stages[4] = { 1, 1, 1, 1 };
  double fcOff = 0.3, kScale = 1, cComp = 1, vcaGain = 1, bleed = 0, pwmSlope = 1, pwmOff = 0, noiseGain = 1;
  double aSaw = 1, gSaw = 1 / 72.0, aPulse = 1, gPulse = 1 / 43.4, a8 = 1, g8 = 1 / 72.0, a16 = 1, g16 = 1 / 72.0;
};
struct SynthAlpProfile {
  double hp[2] = { 1, 1 };
  double hpOff = 0.3, outGain = 1, noiseAmp = 1, pwmDepth = 1, modVco = 1, modVcf = 1;
};
struct SynthProfile {
  bool ideal = true;
  SynthAlpProfile alp;
  SynthVoiceProfile voices[6];
};

class Synth {
public:
  explicit Synth(double fs);
  void setDevice(const SynthProfile* prof);       // nullptr or ideal = ideal unit
  void setFrame(const float* frame);              // F::LEN values
  void process(float* out, int n);                // mono
  void setPhases(double p);                       // for tests: all oscillator phases (JS: Math.random())

  double master = 0.7;
  double peak = 0;

private:
  struct Voice { double phase = 0; int count = 0; double s1 = 0, s2 = 0, s3 = 0, s4 = 0, dcx = 0, dcy = 0; };
  struct VC {
    double dt = 0, G1 = 0, G2 = 0, G3 = 0, G4 = 0, P = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 1, k = 0, kc = 1,
           amp = 0, duty = 0.5, nz = 1, wSaw = 0, wPulse = 0, w8 = 0, w16 = 0;
  };
  double fs;
  Voice voices[6];
  VC vc[6];
  float target[F::LEN], cur[F::LEN];               // Float32Array in the JS version
  double hp1 = 0, hp2 = 0;
  uint32_t seed = 22222;
  int block = 32;
  SynthAlpProfile alp;
  SynthVoiceProfile dev[6];
  double noise();
};

} // namespace t6v
