// Component tolerances - C++ port of emulator/js/tolerance.js. Keep both in step:
// the random numbers must be drawn in exactly the same order, otherwise a serial number
// gives a different unit than in the WebApp.
#include "device.h"
#include <cmath>
#include <map>
#include <vector>
#include <algorithm>

namespace t6v {

namespace {
struct PartDef { const char* ref; double nom; const char* kind; const char* cls; const char* text; };
#include "gen/parts_data.inc"

uint32_t hashStr(const std::string& s) {         // FNV-1a over the characters (ASCII serials)
  uint32_t h = 2166136261u;
  for (unsigned char ch : s) { h ^= ch; h *= 16777619u; }
  return h;
}

struct Rng {                                     // mulberry32, as in tolerance.js
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  double next() {
    uint32_t t = (s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (t ^ (t >> 14)) / 4294967296.0;
  }
  double gauss() {
    double u = std::max(next(), 1e-12), v = next();
    return std::sqrt(-2 * std::log(u)) * std::cos(2 * 3.141592653589793 * v);
  }
  double tn(double mu, double sigma, double lo, double hi) {
    double x = mu + sigma * gauss();
    return std::max(lo, std::min(hi, x));
  }
};

struct Cls { double s, band, mu, ageMu, ageS; };
const Cls* classOf(const std::string& c) {
  static const Cls carbon = { 0.02, 0.05, 0, 0.008, 0.004 };
  static const Cls prec = { 0.004, 0.01, 0, 0.001, 0.0005 };
  static const Cls film = { 0.02, 0.05, 0, -0.002, 0.002 };
  static const Cls mkp = { 0.02, 0.05, 0, -0.003, 0.002 };
  static const Cls ceramic = { 0.08, 0.20, 0, -0.03, 0.02 };
  static const Cls elko = { 0.12, 0.35, 0.10, -0.15, 0.10 };
  static const Cls trim = { 0, 0, 0, 0, 0 };
  if (c == "prec") return &prec;
  if (c == "film") return &film;
  if (c == "mkp") return &mkp;
  if (c == "ceramic") return &ceramic;
  if (c == "elko") return &elko;
  if (c == "trim") return &trim;
  return &carbon;
}
const double DAC_SIGMA[12] = { 0.02, 0.02, 0.03, 0.03, 0.04, 0.05, 0.06, 0.07, 0.09, 0.11, 0.13, 0.16 };

struct Ota { double gm = 1, off = 0; };
struct Part {
  double nom = 0, val = 0;
  bool hasIc = false;
  Ota A, C;                    // OTA halves
  double vbe = 0;              // OTA I_abc pin shift
  std::vector<double> opOff;   // opamp offsets (mV)
  double bitErr[12] = {};      // DAC
  double voh = 1;              // flip-flop
};
using BoardParts = std::map<std::string, Part>;

Part drawPart(const PartDef& p, Rng& r) {
  Part o;
  o.nom = p.nom; o.val = p.nom;
  std::string kind = p.kind, cls = p.cls, text = p.text;
  if ((kind == "R" || kind == "C") && p.nom != 0) {
    const Cls& c = *classOf(cls);
    double prod = r.tn(c.mu, c.s, c.mu - c.band, c.mu + c.band * (cls == "elko" ? 1.43 : 1));
    double age = c.ageMu + c.ageS * r.gauss();
    o.val = p.nom * (1 + prod) * (1 + age);
  } else if (kind == "IC") {
    if (cls == "ota") {
      o.hasIc = true;
      o.A.gm = 1 + r.tn(0, 0.03, -0.08, 0.08); o.A.off = r.tn(0, 0.7, -2, 2);
      o.C.gm = 1 + r.tn(0, 0.03, -0.08, 0.08); o.C.off = r.tn(0, 0.7, -2, 2);
      o.vbe = r.tn(0, 0.03, -0.08, 0.08);
    } else if (cls == "opamp") {
      o.hasIc = true;
      int n = (text.find("074") != std::string::npos || text.find("084") != std::string::npos) ? 4
            : ((text.find("072") != std::string::npos || text.find("082") != std::string::npos) ? 2 : 1);
      for (int k = 0; k < n; k++) o.opOff.push_back(r.tn(0, 2, -6, 6));
    } else if (cls == "dac") {
      o.hasIc = true;
      for (int k = 0; k < 12; k++) o.bitErr[k] = r.tn(0, DAC_SIGMA[k], -2.5 * DAC_SIGMA[k], 2.5 * DAC_SIGMA[k]);
    } else if (cls == "ff") {
      o.hasIc = true;
      o.voh = 1 + r.tn(0, 0.01, -0.03, 0.03);
    } else if (cls == "mux") {
      o.hasIc = true;
      r.tn(0, 1.5, -4, 4);                       // pedestal (not used by the model, keeps the sequence)
    }
  } else if (kind == "Q") {
    r.tn(0, 0.25, -0.5, 0.6); r.tn(0, 8, -20, 20);
  } else if (kind == "D") {
    r.tn(0, 10, -25, 25);
  } else if (kind == "X") {
    r.tn(0, 20, -50, 50);
  }
  return o;
}

template <size_t N>
BoardParts drawBoard(const PartDef (&list)[N], const std::string& seedText, bool ideal) {
  BoardParts map;
  Rng r(hashStr(seedText));
  for (size_t i = 0; i < N; i++) {
    Part o = drawPart(list[i], r);
    if (ideal) { Part id; id.nom = o.nom; id.val = o.nom; o = id; }
    map[list[i].ref] = o;
  }
  return map;
}

struct Builder {
  std::map<std::string, BoardParts> boards;
  bool ideal;
  Rng cal;
  Builder(const std::string& serial, bool ideal_) : ideal(ideal_), cal(hashStr(serial + "/Abgleich")) {
    boards["DLP"] = drawBoard(PARTS_DLP, serial + "/DLP", ideal);
    boards["ALP"] = drawBoard(PARTS_ALP, serial + "/ALP", ideal);
    for (int n = 1; n <= 6; n++) {
      std::string name = "KLP" + std::to_string(n);
      boards[name] = drawBoard(PARTS_KLP, serial + "/" + name, ideal);
    }
  }
  const Part* part(const std::string& b, const std::string& ref) {
    auto& m = boards[b];
    auto it = m.find(ref);
    return it == m.end() ? nullptr : &it->second;
  }
  double rel(const std::string& b, const std::string& ref) {
    const Part* p = part(b, ref);
    return p && p->nom != 0 ? p->val / p->nom : 1;
  }
  double att(const std::string& b, const std::string& rSmall, const std::string& rBig) {   // divider ratio actual / nominal
    const Part* s = part(b, rSmall); const Part* g = part(b, rBig);
    return (s->val / (s->val + g->val)) / (s->nom / (s->nom + g->nom));
  }
  double par(const std::string& b, const std::string& x, const std::string& y) {          // parallel resistance in kOhm
    double a = part(b, x)->val, c = part(b, y)->val;
    return a * c / (a + c) / 1000;
  }
  Ota ota(const std::string& b, const std::string& ref, char half) {
    const Part* p = part(b, ref);
    if (!p || !p->hasIc) return Ota();
    return half == 'A' ? p->A : p->C;
  }
  double calN(double sigma, double band) { return ideal ? 0 : cal.tn(0, sigma, -band, band); }
};
} // namespace

DeviceProfile makeDevice(const std::string& serial, bool ideal) {
  Builder D(serial, ideal);
  DeviceProfile out;
  out.serial = serial;
  for (int n = 1; n <= 6; n++) {
    std::string B = "KLP" + std::to_string(n);
    // VCO: trimmed with RV5 (scale), RV3 (offset), RV4 (linearity) -> residual errors
    double thr = D.att(B, "R23", "R22");
    double scale = 1 + D.calN(0.009, 0.0165);
    double offsetEff = D.calN(1.5, 3);
    double resetUs = D.calN(0.25, 0.6);
    // VCF: 4 OTA stages, RV1 trims the overall cutoff
    static const char* ST[4][6] = { { "C4", "R15", "R10", "R8", "U7", "A" }, { "C7", "R26", "R19", "R18", "U4", "A" },
                                    { "C8", "R39", "R32", "R29", "U4", "C" }, { "C10", "R64", "R58", "R45", "U9", "C" } };
    double m[4];
    for (int s = 0; s < 4; s++)
      m[s] = D.ota(B, ST[s][4], ST[s][5][0]).gm * D.att(B, ST[s][2], ST[s][3]) / (D.rel(B, ST[s][0]) * D.rel(B, ST[s][1]));
    double gmean = std::pow(m[0] * m[1] * m[2] * m[3], 0.25), vcfRes = D.calN(0.015, 0.04);
    const Part* u7 = D.part(B, "U7");
    double u7vbe = u7 && u7->hasIc ? u7->vbe : 0;
    double kScale = 1 + D.calN(0.02, 0.05);
    double cComp = D.att(B, "R70", "R73") / D.att(B, "R69", "R67");
    double vcaGain = D.att(B, "R75", "R74") * D.ota(B, "U5", 'C').gm / D.rel(B, "R79");
    double bleed = ideal ? 0 : std::fabs(D.cal.tn(0, 2e-4, -1e-3, 1e-3));
    const Part* u8 = D.part(B, "U8");
    double voh = u8 && u8->hasIc ? u8->voh : 1;
    const Part* u2 = D.part(B, "U2");
    double u2off = u2 && u2->hasIc && u2->opOff.size() > 1 ? u2->opOff[1] : 0;

    BoardProfile::Voice& bv = out.board.voices[n - 1];
    bv.scale = scale; bv.offsetEff = offsetEff; bv.resetUs = resetUs;
    SynthVoiceProfile& sv = out.synth.voices[n - 1];
    for (int s = 0; s < 4; s++) sv.stages[s] = m[s] / gmean * (1 + vcfRes);
    sv.fcOff = 0.3 + u7vbe;
    sv.kScale = kScale; sv.cComp = cComp; sv.vcaGain = vcaGain; sv.bleed = bleed;
    sv.aSaw = thr * D.att(B, "R31", "R30"); sv.gSaw = 1 / D.par(B, "R30", "R31");
    sv.aPulse = 1; sv.gPulse = 1 / D.par(B, "R51", "R52");
    sv.a8 = voh * D.att(B, "R53", "R49"); sv.g8 = 1 / D.par(B, "R49", "R53");
    sv.a16 = voh * D.att(B, "R54", "R50"); sv.g16 = 1 / D.par(B, "R50", "R54");
    sv.pwmSlope = 1 / D.rel(B, "R3");
    sv.pwmOff = (D.rel(B, "R35") - D.rel(B, "R36")) * 0.25 + u2off / 5000;
    sv.noiseGain = 1 / D.rel(B, "R61");
  }
  // ALP
  const std::string A = "ALP";
  SynthAlpProfile& alp = out.synth.alp;
  alp.hp[0] = D.ota(A, "N201", 'C').gm * D.att(A, "R273", "R277") / (D.rel(A, "C222") * D.rel(A, "R281"));
  alp.hp[1] = D.ota(A, "N201", 'A').gm * D.att(A, "R278", "R279") / (D.rel(A, "C223") * D.rel(A, "R282"));
  const Part* n201 = D.part(A, "N201");
  alp.hpOff = 0.3 + (n201 && n201->hasIc ? n201->vbe : 0);
  alp.outGain = D.ota(A, "N203", 'C').gm * D.att(A, "R257", "R255") * D.rel(A, "R270") / D.rel(A, "R261");
  alp.noiseAmp = 1 + D.calN(0.08, 0.2);
  alp.pwmDepth = 1 + D.calN(0.05, 0.12);
  alp.modVco = D.ota(A, "N204", 'A').gm / D.rel(A, "R229");
  alp.modVcf = D.ota(A, "N204", 'C').gm / D.rel(A, "R230");
  double refGain = 1 + D.calN(0.03, 0.08);
  const Part* dacPart = D.part("DLP", "N104");
  for (int k = 0; k < 12; k++) out.board.bitErr[k] = dacPart && dacPart->hasIc ? dacPart->bitErr[k] : 0;
  out.board.rangeRatio = 8 * (1 + D.calN(0.0005, 0.0015));
  out.board.refGain = refGain;
  out.board.ideal = ideal;
  out.synth.ideal = ideal;
  return out;
}

} // namespace t6v
