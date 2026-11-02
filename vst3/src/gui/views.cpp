#include "views.h"
#include "theme.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "board.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace tiracon {

namespace {
CColor mix(const CColor& a, const CColor& b, double t) {
  t = std::max(0.0, std::min(1.0, t));
  return CColor(uint8_t(a.red + (b.red - a.red) * t), uint8_t(a.green + (b.green - a.green) * t),
                uint8_t(a.blue + (b.blue - a.blue) * t), uint8_t(a.alpha + (b.alpha - a.alpha) * t));
}
void roundRect(CDrawContext* c, const CRect& r, double rad, const CColor& fill, const CColor* frame = nullptr) {
  auto p = owned(c->createGraphicsPath());
  if (!p) { c->setFillColor(fill); c->drawRect(r, kDrawFilled); return; }
  p->addRoundRect(r, rad);
  c->setFillColor(fill);
  c->drawGraphicsPath(p, CDrawContext::kPathFilled);
  if (frame) { c->setFrameColor(*frame); c->setLineWidth(1); c->drawGraphicsPath(p, CDrawContext::kPathStroked); }
}
const char* NOTE_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
std::string noteName(int n) { return std::string(NOTE_NAMES[((n % 12) + 12) % 12]) + std::to_string(n / 12 - 1); }
std::string fmt(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
} // namespace

// ------------------------------------------------------------------ FlatButton
void FlatButton::draw(CDrawContext* c) {
  CRect r = getViewSize();
  CColor face = down ? theme::border : theme::panel;
  CColor fr = active ? theme::accent : theme::border;
  roundRect(c, r, 4, face, &fr);
  theme::label(c, label, r, active ? theme::accent : theme::text, 11, false, kCenterText);
  setDirty(false);
}
void FlatButton::onMouseDownEvent(MouseDownEvent& e) {
  if (!e.buttonState.isLeft()) return;
  down = true; invalid();
  if (onPress) onPress(true);
  e.consumed = true;
}
void FlatButton::onMouseUpEvent(MouseUpEvent& e) {
  if (!down) return;
  down = false; invalid();
  if (onPress) onPress(false);
  if (onClick && getViewSize().pointInside(e.mousePosition)) onClick();
  e.consumed = true;
}
void FlatButton::onMouseCancelEvent(MouseCancelEvent& e) {
  if (down && onPress) onPress(false);
  down = false; invalid();
  e.consumed = true;
}

// ------------------------------------------------------------------ DisplayView
namespace {
const int FONT[10] = { 0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xE6 };
constexpr double DW = 26, DH = 46, DY = 12;
// digit x positions: program 1/0, parameter 3/2, value 5/4, von, bis
constexpr double XP = 14, XPAR = 186, XVAL = 262, XVON = 362, XBIS = 438, DSTEP = 32;
}
void DisplayView::setData(const float* d64, int rangeFrom, int rangeTo) {
  std::copy(d64, d64 + 64, d);
  from = rangeFrom; to = rangeTo;
  invalid();
}
void DisplayView::digit(CDrawContext* c, double x, double y, double w, double h, int seg, const float* lit) {
  // seg = bit mask if lit == nullptr (static digit, fully on), else brightness per bit
  double t = w * 0.17, hh = h / 2;
  auto bright = [&](int bit) { return lit ? double(lit[bit]) : ((seg >> bit) & 1 ? 1.0 : 0.0); };
  auto horiz = [&](double x0, double x1, double yc, int bit) {
    auto p = owned(c->createGraphicsPath());
    p->beginSubpath(CPoint(x0, yc));
    p->addLine(CPoint(x0 + t / 2, yc - t / 2)); p->addLine(CPoint(x1 - t / 2, yc - t / 2)); p->addLine(CPoint(x1, yc));
    p->addLine(CPoint(x1 - t / 2, yc + t / 2)); p->addLine(CPoint(x0 + t / 2, yc + t / 2)); p->closeSubpath();
    c->setFillColor(mix(theme::segOff, theme::segOn, 0.02 + 0.98 * bright(bit)));
    c->drawGraphicsPath(p, CDrawContext::kPathFilled);
  };
  auto vert = [&](double xc, double y0, double y1, int bit) {
    auto p = owned(c->createGraphicsPath());
    p->beginSubpath(CPoint(xc, y0));
    p->addLine(CPoint(xc + t / 2, y0 + t / 2)); p->addLine(CPoint(xc + t / 2, y1 - t / 2)); p->addLine(CPoint(xc, y1));
    p->addLine(CPoint(xc - t / 2, y1 - t / 2)); p->addLine(CPoint(xc - t / 2, y0 + t / 2)); p->closeSubpath();
    c->setFillColor(mix(theme::segOff, theme::segOn, 0.02 + 0.98 * bright(bit)));
    c->drawGraphicsPath(p, CDrawContext::kPathFilled);
  };
  double g = 1.2;                                   // gap between segments
  horiz(x + t / 2 + g, x + w - t / 2 - g, y + t / 2, 7);            // a
  vert(x + w - t / 2, y + t / 2 + g, y + hh - g, 6);                // b
  vert(x + w - t / 2, y + hh + g, y + h - t / 2 - g, 5);            // c
  horiz(x + t / 2 + g, x + w - t / 2 - g, y + h - t / 2, 4);        // d
  vert(x + t / 2, y + hh + g, y + h - t / 2 - g, 3);                // e
  vert(x + t / 2, y + t / 2 + g, y + hh - g, 2);                    // f
  horiz(x + t / 2 + g, x + w - t / 2 - g, y + hh, 1);               // g
  c->setFillColor(mix(theme::segOff, theme::segOn, 0.02 + 0.98 * bright(0)));
  c->drawEllipse(CRect(x + w + 1.5, y + h - t, x + w + 1.5 + t, y + h), kDrawFilled);   // dp
}
void DisplayView::draw(CDrawContext* c) {
  CRect r = getViewSize();
  c->setDrawMode(kAntiAliasing);
  roundRect(c, r, 5, theme::display);
  double ox = r.left, oy = r.top + DY;
  auto pair = [&](double x, int tens, int ones) {
    digit(c, ox + x, oy, DW, DH, 0, d + tens * 8);
    digit(c, ox + x + DSTEP, oy, DW, DH, 0, d + ones * 8);
  };
  pair(XP, 1, 0);
  pair(XPAR, 3, 2);
  pair(XVAL, 5, 4);
  auto staticPair = [&](double x, int n) {
    int tens = n >= 10 ? FONT[(n / 10) % 10] : 0, ones = n >= 0 ? FONT[n % 10] : 0;
    digit(c, ox + x, oy, DW, DH, tens, nullptr);
    digit(c, ox + x + DSTEP, oy, DW, DH, ones, nullptr);
  };
  staticPair(XVON, from);
  staticPair(XBIS, to);
  // slash between value and range
  c->setFrameColor(theme::segOff);
  c->setLineWidth(3);
  c->drawLine(CPoint(ox + 342, oy + DH), CPoint(ox + 350, oy));
  // LEDs (digit 6): LFO VCO a, LFO PWM b, LFO LP c, EDIT d
  struct Led { const char* name; int bit; double x, y; } leds[4] = {
    { "LFO VCO", 7, 88, 16 }, { "LFO PWM", 6, 88, 34 }, { "LFO LP", 5, 88, 52 }, { "EDIT", 4, 140, 52 } };
  for (const Led& l : leds) {
    double v = d[6 * 8 + l.bit];
    c->setFillColor(v > 0.3 ? theme::segOn : theme::rgb(0x26, 0x0d, 0x0b));
    c->drawEllipse(CRect(ox + l.x, r.top + l.y, ox + l.x + 8, r.top + l.y + 8), kDrawFilled);
    theme::label(c, l.name, CRect(ox + l.x + 12, r.top + l.y - 3, ox + l.x + 62, r.top + l.y + 11), theme::dim, 8.5);
  }
  // labels
  double ly = oy + DH + 5;
  auto label = [&](double x, double w, const char* s) { theme::label(c, s, CRect(ox + x, ly, ox + x + w, ly + 12), theme::dim, 8.5, false, kCenterText); };
  label(XP, 58, "PROGRAMM"); label(XPAR, 58, "PARAMETER"); label(XVAL, 58, "WERT"); label(XVON, 58, "VON"); label(XBIS, 58, "BIS");
  setDirty(false);
}

// ------------------------------------------------------------------ InfoView
void InfoView::set(const std::string& n, const std::string& t, const std::vector<std::string>& v, int cur) {
  if (n == no && t == title && v == vals && cur == current) return;
  no = n; title = t; vals = v; current = cur;
  invalid();
}
void InfoView::draw(CDrawContext* c) {
  CRect r = getViewSize();
  c->setDrawMode(kAntiAliasing);
  roundRect(c, r, 4, theme::display);
  double x = r.left + 10;
  if (!no.empty()) {
    theme::label(c, no, CRect(x, r.top, x + 24, r.bottom), theme::accent, 12, true);
    x += 26;
  }
  c->setFont(theme::font(11.5, true));
  theme::label(c, title, CRect(x, r.top, r.right, r.bottom), theme::text, 11.5, true);
  double tw = c->getStringWidth(title.c_str());
  x += tw + 16;
  for (size_t i = 0; i < vals.size(); i++) {
    bool on = int(i) == current;
    std::string s = std::to_string(i) + " " + vals[i];
    c->setFont(theme::font(10.5, on));
    double w = c->getStringWidth(s.c_str());
    if (x + w > r.right - 6) break;
    theme::label(c, s, CRect(x, r.top, x + w + 2, r.bottom), on ? theme::accent : theme::dim, 10.5, on);
    x += w + 12;
  }
  setDirty(false);
}

// ------------------------------------------------------------------ KeypadView
namespace {
struct KeyDef { int code; const char* name; bool dead; };
const KeyDef KEYS[] = {
  { 0x00, "PROG/PARAMETER", false }, { 0x01, "2", false }, { 0x02, "POLY", false }, { 0x03, "SYN START", true },
  { 0x05, "1", false }, { 0x07, "START STOP", true }, { 0x08, "VALUE DOWN", false }, { 0x09, "4", false },
  { 0x0A, "CHORD", false }, { 0x0B, "CONTINUE", true }, { 0x0D, "3", false }, { 0x0F, "ONE START", true },
  { 0x10, "VALUE UP", false }, { 0x11, "6", false }, { 0x12, "HOLD", false }, { 0x13, "RECORD", true },
  { 0x15, "5", false }, { 0x17, "ERASE", true }, { 0x18, "WRITE", false }, { 0x19, "8", false },
  { 0x1A, "ENABLE", false }, { 0x1B, "AUTO TUNE", false }, { 0x1D, "7", false } };
void keyPos(int code, int& row, int& col) {
  int k = code >> 3, bit = code & 7;
  if (bit >= 4) { col = 2 * k; row = bit - 4; } else { col = 2 * k + 1; row = bit; }
}
// the device has PB0..3 = rows; the panel shows them top to bottom as row 0..3
constexpr double KGAP = 6;
}
CRect KeypadView::cell(int row, int col) const {
  CRect r = getViewSize();
  double w = (r.getWidth() - 7 * KGAP) / 8, h = (r.getHeight() - 3 * KGAP) / 4;
  double x = r.left + col * (w + KGAP), y = r.top + row * (h + KGAP);
  return CRect(x, y, x + w, y + h);
}
int KeypadView::codeAt(const CPoint& p) const {
  for (const KeyDef& k : KEYS) {
    int row, col; keyPos(k.code, row, col);
    if (!k.dead && cell(row, col).pointInside(p)) return k.code;
  }
  return -1;
}
void KeypadView::setPressed(int code, bool down) {
  uint32_t m = 1u << code, old = pressed;
  if (down) pressed |= m; else pressed &= ~m;
  if (pressed != old) invalid();
}
void KeypadView::draw(CDrawContext* c) {
  c->setDrawMode(kAntiAliasing);
  for (const KeyDef& k : KEYS) {
    int row, col; keyPos(k.code, row, col);
    CRect r = cell(row, col);
    bool dn = (pressed >> k.code) & 1;
    CColor face = k.dead ? theme::keyDead : (dn ? theme::keyDown : theme::keyFace);
    CColor fr = theme::rgb(0x9a, 0x96, 0x8c);
    roundRect(c, r, 3, face, &fr);
    bool digitKey = k.name[1] == 0;
    CColor tc = k.dead ? theme::rgb(0x3c, 0x3b, 0x39) : theme::keyText;
    std::string name = k.name;
    c->setFont(theme::font(8.5));
    size_t cut = name.find_first_of("/ ");
    if (!digitKey && c->getStringWidth(name.c_str()) > r.getWidth() - 6 && cut != std::string::npos) {
      // two lines: "PROG/" "PARAMETER", "START" "STOP"
      std::string a = name.substr(0, name[cut] == '/' ? cut + 1 : cut), b = name.substr(cut + 1);
      CRect top = r, bottom = r;
      top.bottom = r.getCenter().y + 1; bottom.top = r.getCenter().y - 1;
      top.top += 2; bottom.bottom -= 2;
      theme::label(c, a, top, tc, 8, false, kCenterText);
      theme::label(c, b, bottom, tc, 8, false, kCenterText);
    } else {
      theme::label(c, name, r, tc, digitKey ? 14 : 8.5, digitKey, kCenterText);
    }
  }
  setDirty(false);
}
void KeypadView::onMouseDownEvent(MouseDownEvent& e) {
  if (!e.buttonState.isLeft()) return;
  int code = codeAt(e.mousePosition);
  if (code < 0) return;
  held = code;
  setPressed(code, true);
  if (onKey) onKey(code, true);
  e.consumed = true;
}
void KeypadView::onMouseUpEvent(MouseUpEvent& e) {
  if (held < 0) return;
  setPressed(held, false);
  if (onKey) onKey(held, false);
  held = -1;
  e.consumed = true;
}
void KeypadView::onMouseCancelEvent(MouseCancelEvent& e) {
  if (held >= 0) { setPressed(held, false); if (onKey) onKey(held, false); }
  held = -1;
  e.consumed = true;
}

// ------------------------------------------------------------------ ValueSlider
void ValueSlider::set(const std::string& l, const std::string& det, int v, int m, bool en) {
  if (drag) { if (l == label && m == max) { detail = det; invalid(); return; } }
  if (l == label && det == detail && v == value && m == max && en == enabled) return;
  label = l; detail = det; value = v; max = std::max(1, m); enabled = en;
  invalid();
}
CRect ValueSlider::track() const {
  CRect r = getViewSize();
  if (r.getHeight() >= 30) return CRect(r.left + 8, r.bottom - 14, r.right - 8, r.bottom - 6);
  return CRect(r.left + labelW, r.top + r.getHeight() / 2 - 3, r.right - 56, r.top + r.getHeight() / 2 + 3);
}
int ValueSlider::valueAt(double x) const {
  CRect t = track();
  double f = (x - t.left) / std::max(1.0, t.getWidth());
  return std::max(0, std::min(max, int(std::lround(f * max))));
}
void ValueSlider::draw(CDrawContext* c) {
  CRect r = getViewSize();
  c->setDrawMode(kAntiAliasing);
  bool twoLine = r.getHeight() >= 30;
  roundRect(c, r, 4, twoLine ? theme::panel : theme::display);
  CColor tc = enabled ? theme::text : theme::dim;
  if (twoLine) {
    theme::label(c, label, CRect(r.left + 8, r.top + 3, r.right - 8, r.top + 17), tc, 10, true);
    std::string v = enabled ? std::to_string(value) : "";
    theme::label(c, v + (detail.empty() ? "" : "  " + detail), CRect(r.left + 8, r.top + 3, r.right - 8, r.top + 17), theme::accent, 10, false, kRightText);
  } else {
    theme::label(c, label, CRect(r.left + 10, r.top, r.left + labelW - 6, r.bottom), tc, 10.5, false);
    theme::label(c, enabled ? std::to_string(value) : "", CRect(r.right - 50, r.top, r.right - 10, r.bottom), theme::accent, 11, true, kRightText);
  }
  CRect t = track();
  roundRect(c, t, t.getHeight() / 2, theme::barBg);
  if (enabled) {
    double f = double(value) / max;
    CRect fill = t; fill.right = t.left + f * t.getWidth();
    roundRect(c, fill, t.getHeight() / 2, theme::bar);
    double kx = t.left + f * t.getWidth(), ky = t.getCenter().y;
    c->setFillColor(theme::text);
    c->drawEllipse(CRect(kx - 6, ky - 6, kx + 6, ky + 6), kDrawFilled);
  }
  setDirty(false);
}
void ValueSlider::onMouseDownEvent(MouseDownEvent& e) {
  if (!enabled || !e.buttonState.isLeft()) return;
  drag = true;
  if (onEdit) onEdit(value, 0);
  int v = valueAt(e.mousePosition.x);
  if (v != value) { value = v; if (onEdit) onEdit(v, 1); }
  invalid();
  e.consumed = true;
}
void ValueSlider::onMouseMoveEvent(MouseMoveEvent& e) {
  if (!drag) return;
  int v = valueAt(e.mousePosition.x);
  if (v != value) { value = v; if (onEdit) onEdit(v, 1); invalid(); }
  e.consumed = true;
}
void ValueSlider::onMouseUpEvent(MouseUpEvent& e) {
  if (!drag) return;
  drag = false;
  if (onEdit) onEdit(value, 2);
  e.consumed = true;
}
void ValueSlider::onMouseCancelEvent(MouseCancelEvent& e) {
  if (drag && onEdit) onEdit(value, 2);
  drag = false;
  e.consumed = true;
}

// ------------------------------------------------------------------ VoicesView
void VoicesView::draw(CDrawContext* c) {
  CRect r = getViewSize();
  c->setDrawMode(kAntiAliasing);
  roundRect(c, r, 5, theme::panel);
  double x = r.left + 10, y = r.top + 6;
  theme::label(c, snap.instances > 1 ? "STIMMEN (" + std::to_string(snap.instances * 6) + ", VCA)" : std::string("STIMMEN"), CRect(x, y, x + 180, y + 12), theme::dim, 8.5);
  if (snap.instances <= 1) {
    theme::label(c, "VCA", CRect(x + 190, y, x + 240, y + 12), theme::dim, 8.5);
    theme::label(c, "VCF", CRect(x + 244, y, x + 290, y + 12), theme::dim, 8.5);
  }
  y += 14;
  auto barAt = [&](double bx, double by, double w, double v) {
    CRect b(bx, by, bx + w, by + 6);
    roundRect(c, b, 3, theme::barBg);
    if (v > 0.002) { CRect f = b; f.right = b.left + std::min(1.0, v) * w; roundRect(c, f, 3, theme::bar); }
  };
  if (snap.instances <= 1) {
    for (int v = 0; v < 6; v++) {
      const UiSnapshot::Voice& vo = snap.voices[v];
      bool active = vo.state != 0;
      CColor tc = active ? theme::text : theme::dim;
      theme::label(c, "KLP" + std::to_string(v + 1), CRect(x, y, x + 34, y + 15), tc, 10, active);
      theme::label(c, vo.key >= 0 ? noteName(vo.key + 36) : "-", CRect(x + 36, y, x + 66, y + 15), tc, 10);
      if (vo.hz > 1) {
        double n = 69 + 12 * std::log2(vo.hz / 440.0);
        int ni = int(std::lround(n));
        double ct = (n - ni) * 100;
        theme::label(c, fmt("%.1f Hz", vo.hz), CRect(x + 68, y, x + 130, y + 15), tc, 10);
        theme::label(c, noteName(ni) + fmt(" %+.0f", ct), CRect(x + 130, y, x + 186, y + 15), tc, 10);
      }
      barAt(x + 190, y + 5, 48, vo.ega);
      barAt(x + 244, y + 5, 48, vo.egf);
      y += 16;
    }
  } else {
    // one row per Tiracon: 6 cells with the key and the VCA level
    int n = std::min(snap.instances, 8);
    double rowH = 96.0 / n, cw = (r.right - x - 10 - 26) / 6;
    for (int i = 0; i < n; i++) {
      double ry = y + i * rowH;
      theme::label(c, "T" + std::to_string(i + 1), CRect(x, ry, x + 24, ry + rowH), theme::dim, std::min(10.0, rowH - 2));
      for (int v = 0; v < 6; v++) {
        const UiSnapshot::Voice& vo = snap.voices[i * 6 + v];
        double cx = x + 26 + v * cw;
        bool active = vo.state != 0;
        if (rowH >= 14) theme::label(c, vo.key >= 0 && active ? noteName(vo.key + 36) : "", CRect(cx, ry, cx + 26, ry + rowH), theme::text, 9);
        double bx = rowH >= 14 ? cx + 26 : cx;
        barAt(bx, ry + rowH / 2 - 3, cw - (bx - cx) - 4, vo.ega);
      }
    }
    y += 96;
  }
  y += 6;
  theme::label(c, "STEUERSPANNUNGEN", CRect(x, y, x + 200, y + 12), theme::dim, 8.5);
  y += 14;
  struct Row { int cv; const char* name; } rows[] = {
    { t6v::CV_OI, "OI  Ausgang" }, { t6v::CV_NI, "NI  Rauschen" }, { t6v::CV_Q, "Q  Resonanz" }, { t6v::CV_FH, "FH  Hochpass" },
    { t6v::CV_LI1, "LI1  LFO VCO" }, { t6v::CV_LI2, "LI2  PW/PWM" }, { t6v::CV_LI3, "LI3  LFO LP" },
    { t6v::CV_LFO1, "LFO1  VCO" }, { t6v::CV_LFO2, "LFO2  PWM" }, { t6v::CV_LFO3, "LFO3  LP" } };
  for (const Row& row : rows) {
    theme::label(c, row.name, CRect(x, y - 1, x + 110, y + 12), theme::dim, 9.5);
    barAt(x + 110, y + 3, r.right - x - 120, snap.cv[row.cv]);
    y += 13;
  }
  y += 6;
  const char* waves[5] = { "Säge", "Puls", "PWM", "8'", "16'" };
  double cx = x;
  for (int i = 0; i < 5; i++) {
    CRect chip(cx, y, cx + (i < 3 ? 52 : 34), y + 16);
    bool on = snap.wave[i] != 0;
    CColor fr = on ? theme::accent : theme::border;
    roundRect(c, chip, 8, on ? theme::rgb(0x4a, 0x36, 0x14) : theme::panel, &fr);
    theme::label(c, waves[i], chip, on ? theme::accent : theme::dim, 9, false, kCenterText);
    cx = chip.right + 5;
  }
  setDirty(false);
}

} // namespace tiracon
