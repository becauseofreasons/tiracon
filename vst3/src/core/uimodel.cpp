#include "uimodel.h"
#include "engine.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace t6v {

namespace {
struct ParamName { int no, group; const char* title; const char* label; const char* vals; const char* foot; int sym, isNew, glob; };
#include "gen/param_names.inc"

std::vector<std::string> split(const char* s) {
  std::vector<std::string> v;
  std::string cur;
  if (!*s) return v;
  for (const char* p = s; ; p++) {
    if (*p == '|' || *p == 0) { v.push_back(cur); cur.clear(); if (!*p) break; }
    else cur += *p;
  }
  return v;
}
} // namespace

const int DIGIT_KEYS[9] = { -1, 0x05, 0x01, 0x0D, 0x09, 0x15, 0x11, 0x1D, 0x19 };

std::string ParamInfo::valueName(int v) const {
  if (v >= 0 && v < int(vals.size())) return vals[size_t(v)];
  return std::to_string(v);
}

const std::vector<ParamInfo>& allParams() {
  static const std::vector<ParamInfo> list = [] {
    std::vector<ParamInfo> v;
    const RomInfo& r = romInfo();
    for (const ParamName& p : PARAM_NAMES) {
      if (!r.has(p.no)) continue;
      v.push_back({ p.no, p.group, r.max[p.no], p.title, p.label, split(p.vals), p.foot, p.sym, p.isNew != 0, p.glob != 0 });
    }
    return v;
  }();
  return list;
}
const ParamInfo* paramInfo(int no) {
  for (const ParamInfo& p : allParams()) if (p.no == no) return &p;
  return nullptr;
}
const std::vector<std::string>& groupNames() {
  static const std::vector<std::string> g(std::begin(GROUP_NAMES), std::end(GROUP_NAMES));
  return g;
}

// ---------------------------------------------------------------- display (params.js readDisplay)
namespace {
int digitOf(int code) {
  static const int FONT[10] = { 0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xE6 };
  for (int i = 0; i < 10; i++) if (FONT[i] == code) return i;
  return -1;
}
struct Pair { int n; bool single; };
Pair pairOf(int tens, int ones) {               // -1 blank, -2 not a number
  tens &= 0xFE; ones &= 0xFE;
  if (!tens && !ones) return { -1, false };
  int t = digitOf(tens), o = digitOf(ones);
  if (!tens && o >= 0) return { o, true };
  if (t >= 0 && o >= 0) return { t * 10 + o, false };
  return { -2, false };
}
} // namespace

DisplayState readDisplay(const float* d) {
  int c[6];
  for (int i = 0; i < 6; i++) {
    c[i] = 0;
    for (int b = 0; b < 8; b++) if (d[i * 8 + b] > 0.5f) c[i] |= 1 << b;
  }
  DisplayState s;
  if ((c[1] & 0xFE) == 0x02 && (c[0] & 0xFE) == 0x02) { s.mode = DisplayState::Tune; return s; }
  if ((c[3] & 0xFE) == 0x1E && (c[2] & 0xFE) == 0x9E) { s.mode = DisplayState::Service; return s; }
  if ((c[0] | c[1]) & 1) { s.mode = DisplayState::Enable; return s; }
  Pair prog = pairOf(c[1], c[0]), par = pairOf(c[3], c[2]), val = pairOf(c[5], c[4]);
  if (prog.n == -2 || par.n == -2) { s.mode = DisplayState::Other; return s; }
  if (par.n == -1) {
    if (prog.n == -1) { s.mode = DisplayState::Blank; return s; }
    s.mode = DisplayState::Prog; s.prog = prog.n; s.partial = prog.single; return s;
  }
  s.mode = DisplayState::Param; s.prog = prog.n; s.param = par.n; s.partial = par.single;
  s.value = val.n >= 0 ? val.n : -1;
  return s;
}

// ---------------------------------------------------------------- envelopes (params.js egCurve)
namespace {
const double EG_FIRST_S[2] = { 0.0014, 0.0084 };
int egCalls(double from16, double to16, int r) { return std::max(1, int(std::ceil(std::fabs(to16 - from16) / egStep(r)))); }
double egSegTime(double fromL, double toL, int r) { return egCalls(fromL * 2048, toL * 2048, r) * EG_CALL_S; }
double egCallTime(int c, bool filter) { return c > 0 ? EG_FIRST_S[filter ? 1 : 0] + (c - 1) * EG_CALL_S : 0; }
}
double egStep(int r) { if (r < 2) return 1; return ((r & 1) ? 3 : 2) * std::pow(2.0, (r >> 1) - 1); }
double egFirstCall(bool filter) { return EG_FIRST_S[filter ? 1 : 0]; }

EgCurve egCurve(const EgParams& p, bool filter, double hold) {
  EgCurve out;
  double rest = filter ? p.L4 : 0, lvl = rest * 2048;
  int calls = 0;
  out.pts.push_back({ 0, rest });
  struct Plan { const char* n; int r, l; } plan[3] = { { "R1", p.R1, p.L1 }, { "R2", p.R2, p.L2 }, { "R3", p.R3, p.L3 } };
  if (hold < 0) {
    int c = 0; double from = rest;
    for (const Plan& s : plan) { c += egCalls(from * 2048, s.l * 2048.0, s.r); from = s.l; }
    double full = egCallTime(c, filter);
    hold = full + std::max(0.15, full * 0.35);
  }
  double first = EG_FIRST_S[filter ? 1 : 0];
  int held = hold > first ? int(std::floor((hold - first) / EG_CALL_S)) + 1 : 0;
  for (const Plan& s : plan) {
    double target = s.l * 2048.0, step = egStep(s.r);
    int n = egCalls(lvl, target, s.r);
    double t0 = egCallTime(calls, filter), l0 = lvl / 2048;
    if (calls + n > held) {                      // key released during this phase
      int k = held - calls;
      lvl = lvl < target ? std::min(target, lvl + k * step) : std::max(target, lvl - k * step);
      calls = held;
      out.pts.push_back({ std::max(t0, egCallTime(calls, filter)), lvl / 2048 });
      out.segs.push_back({ s.n, t0, hold, l0, lvl / 2048, true });
      goto release;
    }
    calls += n; lvl = target;
    out.pts.push_back({ egCallTime(calls, filter), double(s.l) });
    out.segs.push_back({ s.n, t0, egCallTime(calls, filter), l0, double(s.l), false });
  }
release:
  out.pts.push_back({ hold, lvl / 2048 });
  double end = first + held * EG_CALL_S + (egCalls(lvl, rest * 2048, p.R4) - 1) * EG_CALL_S;
  out.segs.push_back({ "R4", hold, end, lvl / 2048, rest, false });
  out.pts.push_back({ end, rest });
  out.hold = hold; out.end = end;
  return out;
}

int egRateFor(double fromL, double toL, double seconds, int prefer) {
  int best = 0;
  double bestErr = std::numeric_limits<double>::infinity();
  for (int r = 0; r <= 31; r++) {
    double err = std::fabs(std::log(egSegTime(fromL, toL, r) / std::max(seconds, 1e-4)));
    if (err < bestErr - 1e-9 || (std::fabs(err - bestErr) < 1e-9 && std::abs(r - prefer) < std::abs(best - prefer))) { best = r; bestErr = err; }
  }
  return best;
}

double vcaDb(double level) {
  const unsigned char* r = FIRMWARE_ROM;
  auto gain = [&](int h) {
    int a = h >> 1, o = 0x0A9C + ((a + (a >> 1)) & 0xFE), w = r[o] | r[o + 1] << 8;
    return double(w & 0xFFFE) / ((w & 1) ? 8 : 1);
  };
  double g = gain(int(std::lround(level * 8))), top = gain(248);
  return g > 0 && top > 0 ? 20 * std::log10(g / top) : -std::numeric_limits<double>::infinity();
}

} // namespace t6v
