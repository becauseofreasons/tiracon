// What the editor needs to know about the firmware - C++ port of the relevant parts of
// emulator/js/params.js: parameter list (from the ROM tables), display decoding, envelope model.
#pragma once
#include "firmware.h"
#include <string>
#include <vector>

namespace t6v {

struct ParamInfo {
  int no;                     // parameter number
  int group;                  // index into groupNames()
  int max;                    // values 0..max (from the ROM)
  std::string title;          // "LOW PASS CUTOFF"
  std::string label;          // sheet label "CUTOFF"
  std::vector<std::string> vals;   // value names (may be empty)
  std::string foot;           // "4'", "8'", "16'" for the waveform cells
  int sym;                    // 0 none, 1 saw, 2 pulse, 3 square
  bool isNew, global;
  std::string valueName(int v) const;   // "PWM", or the number
};
const std::vector<ParamInfo>& allParams();        // all parameters of the built-in firmware, sheet order
const ParamInfo* paramInfo(int no);
const std::vector<std::string>& groupNames();

// ---- 7-segment display (64 brightness values: digit * 8 + bit, bit 7 = a ... bit 1 = g, bit 0 = dp)
struct DisplayState {
  enum Mode { Blank, Prog, Param, Enable, Tune, Service, Other } mode = Blank;
  int prog = -1, param = -1, value = -1;
  bool partial = false;       // only the first digit entered
  bool operator==(const DisplayState& o) const {
    return mode == o.mode && prog == o.prog && param == o.param && value == o.value && partial == o.partial;
  }
};
DisplayState readDisplay(const float* d64);
inline bool isProgramNo(int n) { int b = n / 10, s = n % 10; return b >= 1 && b <= 4 && s >= 1 && s <= 8; }
extern const int DIGIT_KEYS[9];               // BLP key code of the digit keys 1..8 (index 0 unused)
constexpr int KEY_PROG = 0x00, KEY_VALUE_DOWN = 0x08, KEY_VALUE_UP = 0x10, KEY_WRITE = 0x18,
              KEY_ENABLE = 0x1A, KEY_AUTO_TUNE = 0x1B, KEY_POLY = 0x02, KEY_CHORD = 0x0A, KEY_HOLD = 0x12;

// ---- envelope generator of the firmware (see params.js egCurve)
struct EgParams { int R1 = 0, L1 = 0, R2 = 0, L2 = 0, R3 = 0, L3 = 0, R4 = 0, L4 = 0; };
struct EgSeg { const char* name; double t0, t1, l0, l1; bool cut; };
struct EgCurve { std::vector<std::pair<double, double>> pts; std::vector<EgSeg> segs; double hold = 0, end = 0; };
constexpr double EG_CALL_S = 0.01113;
double egStep(int r);
double egFirstCall(bool filter);
EgCurve egCurve(const EgParams& p, bool filter, double hold /* < 0: automatic */);
int egRateFor(double fromL, double toL, double seconds, int prefer);
double vcaDb(double level);                    // VCA gain of a level 0..31 in dB (from CURVE_TAB), -inf = off

} // namespace t6v
