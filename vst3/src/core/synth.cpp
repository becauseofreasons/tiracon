// Tiracon 6V sound model - C++ port of emulator/js/synth-worklet.js. Keep both in step.
// Formulas and component references: see the header comment of synth-worklet.js and docs/Analyse.md 11.
#include "synth.h"
#include <cmath>
#include <algorithm>

namespace t6v {

namespace {
constexpr double MOD_VCO = 0.5;
constexpr double MOD_VCF = 0.8;
constexpr double NOISE_LEVEL = 0.35;
constexpr double PI = 3.141592653589793;

inline double tptG(double fc, double fs) {        // one-pole TPT coefficient g/(1+g), 1 Hz .. 0.45 fs
  double g = std::tan(PI * std::max(1.0, std::min(fc, 0.45 * fs)) / fs);
  return g / (1 + g);
}
inline double polyBlep(double t, double dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}
inline double jsMod1(double x) { return std::fmod(x, 1.0); }   // JS x % 1
} // namespace

Synth::Synth(double fs_) : fs(fs_) {
  for (int i = 0; i < F::LEN; i++) target[i] = 0;
  target[F::LFO1] = target[F::LFO2] = target[F::LFO3] = 0.5f;
  for (int i = 0; i < F::LEN; i++) cur[i] = target[i];
  // the JS version starts every oscillator at Math.random(); here a fixed spread (deterministic)
  uint32_t r = 0x9E3779B9u;
  for (Voice& v : voices) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; v.phase = r / 4294967296.0; }
  setDevice(nullptr);
}

void Synth::setPhases(double p) { for (Voice& v : voices) v.phase = p; }

void Synth::setDevice(const SynthProfile* prof) {
  bool real = prof && !prof->ideal;
  alp = real ? prof->alp : SynthAlpProfile();
  for (int v = 0; v < 6; v++) dev[v] = real ? prof->voices[v] : SynthVoiceProfile();
}

void Synth::setFrame(const float* frame) {
  for (int i = 0; i < F::LEN; i++) target[i] = frame[i];
}

double Synth::noise() {                            // xorshift32 -> -1..1
  uint32_t x = seed;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  seed = x;
  return x / 2147483648.0 - 1;
}

void Synth::process(float* out, int n) {
  const int B = block;
  for (int off = 0; off < n; off += B) {
    int len = std::min(B, n - off);
    // control-rate smoothing (S&H outputs + their buffers); levels ~3 ms, pitch ~1 ms
    double aL = 1 - std::exp(-len / (fs * 0.003)), aP = 1 - std::exp(-len / (fs * 0.001));
    for (int i = 0; i < F::LEN; i++) {
      if (i >= F::SSZ) { cur[i] = target[i]; continue; }
      cur[i] = float(cur[i] + (double(target[i]) - cur[i]) * (i < F::EGA ? aP : aL));
    }
    double k = 4.25 * (0.3 + 27 * double(cur[F::Q])) / 27.3;
    double fcHp = 1731 * (alp.hpOff / 27 + double(cur[F::FH]));
    double GHp1 = tptG(fcHp * alp.hp[0], fs), GHp2 = tptG(fcHp * alp.hp[1], fs);
    double A = 4.7 * (0.3 + 27 * double(cur[F::OI])) / 27.3 * alp.outGain, outGain = A / (1 + A);
    double noiseAmp = NOISE_LEVEL * double(cur[F::NI]) * alp.noiseAmp;
    double vibrato = 1 + MOD_VCO * alp.modVco * double(cur[F::LI1]) * (2 * double(cur[F::LFO1]) - 1);
    double fmod = 1 + MOD_VCF * alp.modVcf * double(cur[F::LI3]) * (2 * double(cur[F::LFO3]) - 1);
    double pw = 0.47 * std::min(1.0, double(cur[F::LI2]) * double(cur[F::LFO2])) * alp.pwmDepth;
    bool onSaw = cur[F::SSZ] > 0.5f, onPulse = cur[F::SRE] > 0.5f, on8 = cur[F::SF2] > 0.5f, on16 = cur[F::SF4] > 0.5f;

    for (int v = 0; v < 6; v++) {
      const SynthVoiceProfile& t = dev[v];
      VC& cv = vc[v];
      const double* st = t.stages;
      double hz = double(cur[F::HZ + v]) * vibrato;
      double egf = std::max(0.0, double(cur[F::EGF + v]) * fmod);
      double fc = 16000 * (t.fcOff + 27 * egf) / 27.3;
      double G1 = tptG(fc * st[0], fs), G2 = tptG(fc * st[1], fs), G3 = tptG(fc * st[2], fs), G4 = tptG(fc * st[3], fs);
      cv.G1 = G1; cv.G2 = G2; cv.G3 = G3; cv.G4 = G4;
      // zero delay feedback solution of the ladder with four different stages: y4 = P*u + S
      cv.P = G1 * G2 * G3 * G4;
      cv.a1 = G2 * G3 * G4 * (1 - G1); cv.a2 = G3 * G4 * (1 - G2); cv.a3 = G4 * (1 - G3); cv.a4 = 1 - G4;
      cv.k = k * t.kScale; cv.kc = 1 + cv.k * t.cComp;
      cv.dt = std::min(std::max(hz, 0.0) / fs, 0.45);
      cv.amp = double(cur[F::EGA + v]) * t.vcaGain + t.bleed;      // the OTA VCA never closes completely
      cv.duty = std::max(0.03, std::min(0.97, 0.5 - pw * t.pwmSlope + t.pwmOff));
      cv.nz = t.noiseGain;
      double gSum = (onSaw ? t.gSaw : 0) + (onPulse ? t.gPulse : 0) + (on8 ? t.g8 : 0) + (on16 ? t.g16 : 0) + 1 / 1200.0;
      cv.wSaw = onSaw ? t.aSaw * t.gSaw / gSum : 0; cv.wPulse = onPulse ? t.aPulse * t.gPulse / gSum : 0;
      cv.w8 = on8 ? t.a8 * t.g8 / gSum : 0; cv.w16 = on16 ? t.a16 * t.g16 / gSum : 0;
    }

    for (int j = 0; j < len; j++) {
      double nz = noise() * noiseAmp, sum = 0;
      for (int v = 0; v < 6; v++) {
        Voice& vo = voices[v];
        const VC& c = vc[v];
        if (c.amp <= 1e-5) {                     // VCA closed: keep the oscillator (and divider) running only
          vo.phase += c.dt; if (vo.phase >= 1) { vo.phase -= 1; vo.count = (vo.count + 1) & 3; }
          continue;
        }
        double dt = c.dt, p = vo.phase, duty = c.duty, wSaw = c.wSaw, wPulse = c.wPulse, w8 = c.w8, w16 = c.w16;
        double x = 0;
        if (dt > 0) {
          if (wSaw != 0) x += wSaw * (2 * p - 1 - polyBlep(p, dt));
          if (wPulse != 0) {
            double pu = p < duty ? 1 : -1;
            pu += polyBlep(p, dt) - polyBlep(jsMod1(p - duty + 1), dt);
            x += wPulse * pu;
          }
          if (w8 != 0) {
            double p8 = ((vo.count & 1) + p) * 0.5, d8 = dt * 0.5;
            x += w8 * ((p8 < 0.5 ? 1 : -1) + polyBlep(p8, d8) - polyBlep(jsMod1(p8 + 0.5), d8));
          }
          if (w16 != 0) {
            double p16 = ((vo.count & 3) + p) * 0.25, d16 = dt * 0.25;
            x += w16 * ((p16 < 0.5 ? 1 : -1) + polyBlep(p16, d16) - polyBlep(jsMod1(p16 + 0.5), d16));
          }
          p += dt;
          if (p >= 1) { p -= 1; vo.count = (vo.count + 1) & 3; }
          vo.phase = p;
        }
        // AC coupling (C9 1 uF / 68k)
        double dc = x - vo.dcx + 0.9997 * vo.dcy; vo.dcx = x; vo.dcy = dc;
        double inp = dc + nz * c.nz;
        // VCF: zero delay feedback 4-pole ladder with gain-compensated resonance
        double S = c.a1 * vo.s1 + c.a2 * vo.s2 + c.a3 * vo.s3 + c.a4 * vo.s4;
        double y4 = (c.P * c.kc * inp + S) / (1 + c.k * c.P);
        double u = c.kc * inp - c.k * y4;
        u = 3 * std::tanh(u / 3);                 // OTA input stage limits the loop
        double t1 = (u - vo.s1) * c.G1, y1 = t1 + vo.s1; vo.s1 = y1 + t1;
        double t2 = (y1 - vo.s2) * c.G2, y2 = t2 + vo.s2; vo.s2 = y2 + t2;
        double t3 = (y2 - vo.s3) * c.G3, y3 = t3 + vo.s3; vo.s3 = y3 + t3;
        double t4 = (y3 - vo.s4) * c.G4, y = t4 + vo.s4; vo.s4 = y + t4;
        sum += y * c.amp;                         // VCA
      }
      // ALP: summing point, 12 dB high pass (2 x CR), output VCA
      double h1v = (sum - hp1) * GHp1, h1l = h1v + hp1; hp1 = h1l + h1v;
      double h1 = sum - h1l;
      double h2v = (h1 - hp2) * GHp2, h2l = h2v + hp2; hp2 = h2l + h2v;
      double h2 = h1 - h2l;
      double o = h2 * outGain * master * 0.6;
      o = std::tanh(o);                           // soft limiter instead of digital clipping
      double ao = o < 0 ? -o : o;
      if (ao > peak) peak = ao;
      out[off + j] = float(o);
    }
  }
}

} // namespace t6v
