#include "overlays.h"
#include "theme.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace tiracon {

namespace {
void roundRect(CDrawContext* c, const CRect& r, double rad, const CColor& fill, const CColor* frame = nullptr) {
  auto p = owned(c->createGraphicsPath());
  p->addRoundRect(r, rad);
  c->setFillColor(fill);
  c->drawGraphicsPath(p, CDrawContext::kPathFilled);
  if (frame) { c->setFrameColor(*frame); c->setLineWidth(1); c->drawGraphicsPath(p, CDrawContext::kPathStroked); }
}
void closeButton(CDrawContext* c, const CRect& r, const CColor& col) {
  c->setFrameColor(col);
  c->setLineWidth(2);
  c->drawLine(CPoint(r.left + 6, r.top + 6), CPoint(r.right - 6, r.bottom - 6));
  c->drawLine(CPoint(r.right - 6, r.top + 6), CPoint(r.left + 6, r.bottom - 6));
}
std::string fmtT(double s) {
  char b[32];
  if (s < 0.9995) std::snprintf(b, sizeof b, "%d ms", int(std::lround(s * 1000)));
  else if (s < 9.995) std::snprintf(b, sizeof b, "%.2f s", s);
  else if (s < 59.95) std::snprintf(b, sizeof b, "%.1f s", s);
  else std::snprintf(b, sizeof b, "%d:%02d min", int(s / 60), int(std::lround(s - 60 * std::floor(s / 60))) % 60);
  std::string r = b;
  for (char& ch : r) if (ch == '.') ch = ',';
  return r;
}
std::string fmtDb(double db) {
  if (!std::isfinite(db)) return "aus";
  char b[32];
  std::snprintf(b, sizeof b, "%.1f dB", db);
  std::string r = b;
  for (char& ch : r) if (ch == '.') ch = ',';
  if (r == "-0,0 dB") r = "0 dB";
  return r;
}
// sheet rows: groups by index into groupNames()
const std::vector<std::vector<int>> SHEET_ROWS = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 8, 9 }, { 10, 11, 12, 13 } };
constexpr double CW = 50, CH = 56, CG = 2, GG = 10, GH = 14, ROWH = 76, TOP = 24;
} // namespace

// ------------------------------------------------------------------ SheetView
void SheetView::layout() {
  cells.clear();
  CRect r = getViewSize();
  closeRect = CRect(r.right - 30, r.top + 4, r.right - 8, r.top + 22);
  const auto& all = t6v::allParams();
  for (size_t row = 0; row < SHEET_ROWS.size(); row++) {
    double x = r.left + 12, y = r.top + TOP + row * ROWH;
    for (int g : SHEET_ROWS[row]) {
      int n = 0;
      for (const t6v::ParamInfo& p : all) if (p.group == g) {
        cells[p.no] = CRect(x + n * (CW + CG), y + GH + 2, x + n * (CW + CG) + CW, y + GH + 2 + CH);
        n++;
      }
      if (n) x += n * (CW + CG) - CG + GG;
    }
  }
}
void SheetView::draw(CDrawContext* c) {
  layout();
  CRect r = getViewSize();
  c->setDrawMode(kAntiAliasing);
  roundRect(c, r, 6, theme::paper);
  theme::label(c, "Parameterliste", CRect(r.left + 12, r.top + 4, r.left + 200, r.top + 22), theme::ink, 12, true);
  if (!msg.empty()) theme::label(c, msg, CRect(r.left + 140, r.top + 4, r.right - 40, r.top + 22), theme::newRed, 10.5);
  closeButton(c, closeRect, theme::ink);
  const auto& all = t6v::allParams();
  const auto& gn = t6v::groupNames();
  // group headers
  for (size_t row = 0; row < SHEET_ROWS.size(); row++) {
    for (int g : SHEET_ROWS[row]) {
      CRect span; bool any = false;
      for (const t6v::ParamInfo& p : all) if (p.group == g) {
        const CRect& cr = cells[p.no];
        if (!any) { span = cr; any = true; } else span.unite(cr);
      }
      if (!any) continue;
      CRect h(span.left, span.top - GH - 2, span.right, span.top - 2);
      c->setFillColor(theme::ink);
      c->drawRect(h, kDrawFilled);
      theme::label(c, gn[size_t(g)], CRect(h.left + 4, h.top, h.right, h.bottom), theme::paper, 9.5, true);
    }
  }
  // cells
  for (const t6v::ParamInfo& p : all) {
    CRect cr = cells[p.no];
    bool isCur = p.no == cur;
    c->setFillColor(theme::rgb(255, 255, 255));
    c->drawRect(cr, kDrawFilled);
    c->setFrameColor(isCur ? theme::accent : theme::ink);
    c->setLineWidth(isCur ? 2.5 : 1);
    c->drawRect(cr, kDrawStroked);
    theme::label(c, std::to_string(p.no), CRect(cr.left + 4, cr.top + 1, cr.right, cr.top + 14), p.isNew ? theme::newRed : theme::ink, 10, true);
    c->setFrameColor(theme::ink); c->setLineWidth(1);
    c->drawLine(CPoint(cr.left, cr.top + 14), CPoint(cr.right, cr.top + 14));
    c->drawLine(CPoint(cr.left, cr.bottom - 12), CPoint(cr.right, cr.bottom - 12));
    // middle: value list or range
    CRect mid(cr.left + 3, cr.top + 15, cr.right - 2, cr.bottom - 12);
    if (!p.vals.empty()) {
      int n = std::min(int(p.vals.size()) - 1, p.max);
      double lh = std::min(8.5, mid.getHeight() / (n + 1));
      for (int v = n; v >= 0; v--) {
        double y = mid.bottom - (v + 1) * lh;
        bool on = isCur && v == curVal;
        if (on) { c->setFillColor(theme::rgb(0xf6, 0xd5, 0x9a)); c->drawRect(CRect(mid.left - 2, y, mid.right, y + lh), kDrawFilled); }
        theme::label(c, std::to_string(v) + " " + p.vals[size_t(v)], CRect(mid.left, y, mid.right, y + lh), theme::ink, 7, on);
      }
    } else {
      theme::label(c, std::to_string(p.max), CRect(mid.left, mid.top, mid.right, mid.top + 10), theme::ink, 7.5);
      theme::label(c, "0", CRect(mid.left, mid.bottom - 10, mid.right, mid.bottom), theme::ink, 7.5);
      double ax = mid.right - 7;
      c->drawLine(CPoint(ax, mid.top + 3), CPoint(ax, mid.bottom - 2));
      c->drawLine(CPoint(ax - 3, mid.top + 6), CPoint(ax, mid.top + 2));
      c->drawLine(CPoint(ax + 3, mid.top + 6), CPoint(ax, mid.top + 2));
      if (isCur && curVal >= 0 && curVal <= p.max) {
        double y = mid.bottom - 6 - (mid.getHeight() - 12) * curVal / std::max(1, p.max);
        CRect tag(mid.left + 14, y - 6, mid.right - 10, y + 6);
        c->setFillColor(theme::rgb(0xf6, 0xd5, 0x9a)); c->drawRect(tag, kDrawFilled);
        theme::label(c, std::to_string(curVal), tag, theme::ink, 8.5, true, kCenterText);
      }
    }
    std::string lab = p.foot.empty() ? p.label : p.label.substr(0, p.label.find(' ')) + " " + p.foot;
    theme::label(c, lab, CRect(cr.left + 2, cr.bottom - 12, cr.right - 1, cr.bottom), theme::ink, 7, false, kCenterText);
  }
  setDirty(false);
}
void SheetView::onMouseDownEvent(MouseDownEvent& e) {
  e.consumed = true;
  if (!e.buttonState.isLeft()) return;
  if (closeRect.pointInside(e.mousePosition)) { if (onClose) onClose(); return; }
  for (auto& kv : cells) if (kv.second.pointInside(e.mousePosition)) { if (onSelect) onSelect(kv.first); return; }
}

// ------------------------------------------------------------------ EgView
namespace {
// [vca/filter] parameter numbers in the order R1 L1 R2 L2 R3 L3 R4 L4 (-1 = none)
const int EGP[2][8] = { { 51, 52, 53, 54, 55, 56, 57, -1 }, { 61, 62, 63, 64, 65, 66, 67, 68 } };
const char* EGK[8] = { "R1", "L1", "R2", "L2", "R3", "L3", "R4", "L4" };
const char* EGN[8] = { "Attack", "Spitze", "Decay", "Break Point", "Slope", "Sustain", "Release", "Start/Ende" };
const int RAM_ST[2] = { 0x58, 0x6A }, RAM_LV[2] = { 0x64, 0x76 };
const double TICKS[] = { 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200 };
}

void EgView::layout() {
  CRect r = getViewSize();
  closeRect = CRect(r.right - 30, r.top + 4, r.right - 8, r.top + 22);
  tabRect[0] = CRect(r.left + 12, r.top + 5, r.left + 112, r.top + 23);
  tabRect[1] = CRect(r.left + 116, r.top + 5, r.left + 236, r.top + 23);
  graph = CRect(r.left + 44, r.top + 38, r.right - 64, r.top + 218);
  int n = filter ? 8 : 7;
  double sw = (r.getWidth() - 24 - (8 - 1) * 6) / 8;
  for (int k = 0; k < 8; k++) {
    double x = r.left + 12 + k * (sw + 6);
    sliderRect[k] = k < n ? CRect(x, r.top + 252, x + sw, r.top + 288) : CRect();
  }
  testRect = CRect(r.left + 12, r.top + 298, r.left + 132, r.top + 318);
}
t6v::EgParams EgView::params(bool filt) const {
  const int* v = vals[filt ? 1 : 0];
  t6v::EgParams p;
  p.R1 = v[0]; p.L1 = v[1]; p.R2 = v[2]; p.L2 = v[3]; p.R3 = v[4]; p.L3 = v[5]; p.R4 = v[6]; p.L4 = filt ? v[7] : 0;
  return p;
}
double EgView::xOf(double t) const { return graph.left + t / view * graph.getWidth(); }
double EgView::yOf(double l) const { return graph.bottom - l / 31 * graph.getHeight(); }

void EgView::reset() {
  for (auto& a : prevState) for (int& v : a) v = -1;           // voices already sounding are no new key press
  live[0] = Live(); live[1] = Live();
  drag = None;
}

void EgView::setVal(int k, int v) {
  int f = filter ? 1 : 0, no = EGP[f][k];
  v = std::max(0, std::min(31, v));
  if (no < 0 || vals[f][k] == v) return;
  vals[f][k] = v;
  localUntil[f][k] = now + 0.9;
  if (host.editParam) { host.editParam(no, v, 0); host.editParam(no, v, 1); host.editParam(no, v, 2); }
  invalid();
}

// live trace of the voice that played last (egedit.js track)
void EgView::track(const UiSnapshot& s) {
  for (int m = 0; m < 2; m++) {
    bool filt = m == 1;
    for (int v = 0; v < 6; v++) {
      int st = s.ram[RAM_ST[m] + v];
      if (st >= 2 && prevState[m][v] >= 0 && prevState[m][v] < 2) {
        t6v::EgParams q = params(filt);
        double rest = filt ? q.L4 : 0, since;
        if (st == 4) since = t6v::egFirstCall(filt) + std::max(0.0, std::ceil(std::fabs(s.ram[RAM_LV[m] + v] / 8.0 - rest) * 2048 / t6v::egStep(q.R1)) - 1) * t6v::EG_CALL_S;
        else since = t6v::egCurve(q, filt, -1).segs[0].t1 + 0.04;
        live[m] = Live();
        live[m].voice = v; live[m].t0 = now - since; live[m].done = false;
      }
      prevState[m][v] = st;
    }
    Live& L = live[m];
    if (L.voice >= 0 && !L.done) {
      int stv = s.ram[RAM_ST[m] + L.voice];
      double lvNow = s.ram[RAM_LV[m] + L.voice] / 8.0;
      if (stv == 1 && L.rel < 0) {
        t6v::EgParams q = params(filt);
        double back = 0.04, last = L.pts.empty() ? -1 : L.pts.back().second;
        if (last == q.L3) back = std::max(0.0, std::ceil(std::fabs(q.L3 - lvNow) * 2048 / t6v::egStep(q.R4)) - 0.5) * t6v::EG_CALL_S;
        L.rel = std::max(0.0, now - L.t0 - back);
      }
      if (L.pts.size() < 20000) L.pts.push_back({ std::max(0.0, now - L.t0), lvNow });
      if (stv == 0) L.done = true;
    }
  }
}
double EgView::holdTime(const t6v::EgParams& p) const {
  const Live& L = live[filter ? 1 : 0];
  if (L.voice < 0) return -1;
  if (L.rel >= 0) return L.rel;
  if (L.done) return -1;
  return std::max(t6v::egCurve(p, filter, -1).hold, now - L.t0);
}

void EgView::update(const UiSnapshot& s) {
  now = s.time;
  prog = t6v::bcd(s.ram[t6v::RAM_PROGNO]);
  const t6v::RomInfo& rom = t6v::romInfo();
  for (int m = 0; m < 2; m++)
    for (int k = 0; k < 8; k++) {
      int no = EGP[m][k];
      if (no < 0 || now < localUntil[m][k]) continue;
      if (drag != None && m == (filter ? 1 : 0)) continue;
      int v = t6v::ramValue(rom, s.ram, no);
      if (v >= 0) vals[m][k] = std::min(31, v);
    }
  track(s);
  invalid();
}

void EgView::draw(CDrawContext* c) {
  layout();
  CRect r = getViewSize();
  c->setDrawMode(kAntiAliasing);
  roundRect(c, r, 6, theme::panel);
  // tabs, program, close
  const char* tabs[2] = { "EG VCA", "EG LOW PASS" };
  for (int i = 0; i < 2; i++) {
    bool on = (i == 1) == filter;
    CColor fr = on ? theme::accent : theme::border;
    roundRect(c, tabRect[i], 4, on ? theme::rgb(0x4a, 0x36, 0x14) : theme::panel, &fr);
    theme::label(c, tabs[i], tabRect[i], on ? theme::accent : theme::dim, 10.5, on, kCenterText);
  }
  if (t6v::isProgramNo(prog)) theme::label(c, "Programm " + std::to_string(prog), CRect(tabRect[1].right + 14, r.top + 5, r.right - 40, r.top + 23), theme::dim, 10.5);
  closeButton(c, closeRect, theme::text);

  t6v::EgParams p = params(filter);
  t6v::EgCurve cur = t6v::egCurve(p, filter, holdTime(p));
  t6v::EgCurve other = t6v::egCurve(params(!filter), !filter, cur.hold);
  if (drag == None || drag == Slider || drag == Test) view = std::max(0.05, cur.end * 1.06);

  roundRect(c, CRect(graph.left - 2, graph.top - 6, graph.right + 2, graph.bottom + 2), 3, theme::display);
  // grid: levels, dB for the VCA, time ticks
  c->setLineWidth(1);
  for (int l : { 0, 4, 8, 12, 16, 20, 24, 28, 31 }) {
    c->setFrameColor(theme::rgb(0x2a, 0x2d, 0x31));
    c->drawLine(CPoint(graph.left, yOf(l)), CPoint(graph.right, yOf(l)));
    theme::label(c, std::to_string(l), CRect(graph.left - 30, yOf(l) - 6, graph.left - 6, yOf(l) + 6), theme::dim, 9, false, kRightText);
    if (!filter) theme::label(c, fmtDb(t6v::vcaDb(l)), CRect(graph.right + 6, yOf(l) - 6, graph.right + 64, yOf(l) + 6), theme::dim, 9);
  }
  double step = 1200;
  for (double t : TICKS) if (view / t <= 8) { step = t; break; }
  for (double t = 0; t <= view + 1e-9; t += step) {
    c->setFrameColor(theme::rgb(0x2a, 0x2d, 0x31));
    c->drawLine(CPoint(xOf(t), graph.top), CPoint(xOf(t), graph.bottom));
    theme::label(c, fmtT(t), CRect(xOf(t) - 30, graph.bottom + 3, xOf(t) + 30, graph.bottom + 15), theme::dim, 9, false, kCenterText);
  }
  c->saveGlobalState();
  c->setClipRect(CRect(graph.left - 1, graph.top - 8, graph.right + 2, graph.bottom + 1));
  // phase ends, key release
  c->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, { 3, 3 }));
  c->setFrameColor(theme::dim);
  for (const t6v::EgSeg& s : cur.segs)
    if (std::string(s.name) != "R4" && !s.cut) c->drawLine(CPoint(xOf(s.t1), yOf(s.l1)), CPoint(xOf(s.t1), graph.bottom));
  c->setFrameColor(theme::segOn);
  c->drawLine(CPoint(xOf(cur.hold), graph.top), CPoint(xOf(cur.hold), graph.bottom));
  c->setLineStyle(kLineSolid);
  auto poly = [&](const std::vector<std::pair<double, double>>& pts, const CColor& col, double w) {
    if (pts.size() < 2) return;
    auto path = owned(c->createGraphicsPath());
    path->beginSubpath(CPoint(xOf(pts[0].first), yOf(pts[0].second)));
    for (size_t i = 1; i < pts.size(); i++) path->addLine(CPoint(xOf(pts[i].first), yOf(pts[i].second)));
    c->setFrameColor(col); c->setLineWidth(w);
    c->drawGraphicsPath(path, CDrawContext::kPathStroked);
  };
  poly(other.pts, theme::rgb(0x55, 0x5b, 0x62), 1.2);
  poly(cur.pts, theme::text, 2.4);
  const Live& L = live[filter ? 1 : 0];
  if (L.voice >= 0) {
    poly(L.pts, theme::segOn, 1.6);
    if (!L.done && !L.pts.empty()) {
      c->setFillColor(theme::segOn);
      double x = xOf(L.pts.back().first), y = yOf(L.pts.back().second);
      c->drawEllipse(CRect(x - 4, y - 4, x + 4, y + 4), kDrawFilled);
    }
  }
  c->restoreGlobalState();
  // phase names with their duration below the time axis
  for (const t6v::EgSeg& s : cur.segs) {
    double a = std::max(graph.left, xOf(s.t0)), b = std::min(graph.right, xOf(s.t1));
    if (b - a < 16) continue;
    std::string txt = std::string(s.name) + (b - a > 80 ? "  " + fmtT(s.t1 - s.t0) : "");
    theme::label(c, txt, CRect(a, graph.bottom + 16, b, graph.bottom + 30), theme::text, 9.5, false, kCenterText);
  }
  // handles: end of R1 (L1), R2 (L2), R3 (L3), release end (R4), filter start level (L4)
  auto handle = [&](double t, double l, bool active) {
    if (t > view * 1.0001) return;
    double x = xOf(t), y = yOf(l);
    c->setFillColor(active ? theme::accent : theme::rgb(0xf0, 0xa0, 0x30, 120));
    c->drawEllipse(CRect(x - 6, y - 6, x + 6, y + 6), kDrawFilled);
  };
  for (const t6v::EgSeg& s : cur.segs) {
    if (s.cut) continue;
    std::string n = s.name;
    Drag id = n == "R1" ? H_R1 : n == "R2" ? H_R2 : n == "R3" ? H_R3 : H_R4;
    handle(s.t1, s.l1, drag == id);
  }
  if (filter) handle(0, p.L4, drag == H_S);
  // sliders
  int f = filter ? 1 : 0;
  for (int k = 0; k < (filter ? 8 : 7); k++) {
    CRect sr = sliderRect[k];
    roundRect(c, sr, 4, theme::display);
    std::string det;
    if (EGK[k][0] == 'R') {
      for (const t6v::EgSeg& s : cur.segs) if (std::string(s.name) == EGK[k]) det = fmtT(s.t1 - s.t0);
    } else if (!filter) det = fmtDb(t6v::vcaDb(vals[f][k]));
    theme::label(c, std::string(EGK[k]) + " " + EGN[k], CRect(sr.left + 6, sr.top + 2, sr.right - 4, sr.top + 15), theme::text, 9, true);
    theme::label(c, std::to_string(vals[f][k]), CRect(sr.left + 6, sr.top + 13, sr.right - 4, sr.top + 26), theme::accent, 10, true);
    theme::label(c, det, CRect(sr.left + 6, sr.top + 13, sr.right - 6, sr.top + 26), theme::dim, 9, false, kRightText);
    CRect tr(sr.left + 6, sr.bottom - 8, sr.right - 6, sr.bottom - 4);
    roundRect(c, tr, 2, theme::barBg);
    CRect fl = tr; fl.right = tr.left + tr.getWidth() * vals[f][k] / 31.0;
    roundRect(c, fl, 2, theme::bar);
  }
  // test tone
  CColor fr = drag == Test ? theme::accent : theme::border;
  roundRect(c, testRect, 4, drag == Test ? theme::rgb(0x4a, 0x36, 0x14) : theme::display, &fr);
  theme::label(c, "Testton (halten)", testRect, drag == Test ? theme::accent : theme::text, 10, false, kCenterText);
  setDirty(false);
}

void EgView::onMouseDownEvent(MouseDownEvent& e) {
  e.consumed = true;
  if (!e.buttonState.isLeft()) return;
  layout();
  const CPoint& pt = e.mousePosition;
  if (closeRect.pointInside(pt)) { if (host.testTone && drag == Test) host.testTone(false); drag = None; if (host.close) host.close(); return; }
  for (int i = 0; i < 2; i++) if (tabRect[i].pointInside(pt)) { filter = i == 1; invalid(); return; }
  if (testRect.pointInside(pt)) { drag = Test; if (host.testTone) host.testTone(true); invalid(); return; }
  for (int k = 0; k < (filter ? 8 : 7); k++) if (sliderRect[k].pointInside(pt)) {
    drag = Slider; dragSlider = k;
    CRect sr = sliderRect[k];
    setVal(k, int(std::lround((pt.x - sr.left - 6) / (sr.getWidth() - 12) * 31)));
    return;
  }
  // handles
  t6v::EgParams p = params(filter);
  t6v::EgCurve cur = t6v::egCurve(p, filter, holdTime(p));
  auto near = [&](double t, double l) { return std::fabs(xOf(t) - pt.x) < 9 && std::fabs(yOf(l) - pt.y) < 9; };
  for (const t6v::EgSeg& s : cur.segs) {
    if (s.cut || !near(s.t1, s.l1)) continue;
    std::string n = s.name;
    drag = n == "R1" ? H_R1 : n == "R2" ? H_R2 : n == "R3" ? H_R3 : H_R4;
    invalid();
    return;
  }
  if (filter && near(0, p.L4)) { drag = H_S; invalid(); }
}
void EgView::onMouseMoveEvent(MouseMoveEvent& e) {
  if (drag == None || drag == Test) return;
  e.consumed = true;
  const CPoint& pt = e.mousePosition;
  if (drag == Slider) {
    CRect sr = sliderRect[dragSlider];
    setVal(dragSlider, int(std::lround((pt.x - sr.left - 6) / (sr.getWidth() - 12) * 31)));
    return;
  }
  double t = std::max(0.0, (pt.x - graph.left) / graph.getWidth() * view);
  int l = std::max(0, std::min(31, int(std::lround((graph.bottom - pt.y) / graph.getHeight() * 31))));
  t6v::EgParams p = params(filter);
  t6v::EgCurve cur = t6v::egCurve(p, filter, holdTime(p));
  double rest = filter ? p.L4 : 0;
  auto seg = [&](const char* n) { for (const t6v::EgSeg& s : cur.segs) if (std::string(s.name) == n) return s; return cur.segs.back(); };
  if (drag == H_R1) { setVal(1, l); setVal(0, t6v::egRateFor(rest, l, t, p.R1)); }
  else if (drag == H_R2) { setVal(3, l); setVal(2, t6v::egRateFor(p.L1, l, t - seg("R2").t0, p.R2)); }
  else if (drag == H_R3) { setVal(5, l); setVal(4, t6v::egRateFor(p.L2, l, t - seg("R3").t0, p.R3)); }
  else if (drag == H_R4) { if (filter) setVal(7, l); setVal(6, t6v::egRateFor(p.L3, filter ? l : 0, t - cur.hold, p.R4)); }
  else if (drag == H_S) setVal(7, l);
}
void EgView::onMouseUpEvent(MouseUpEvent& e) {
  if (drag == None) return;
  if (drag == Test && host.testTone) host.testTone(false);
  drag = None;
  invalid();
  e.consumed = true;
}
void EgView::onMouseCancelEvent(MouseCancelEvent& e) {
  if (drag == Test && host.testTone) host.testTone(false);
  drag = None;
  e.consumed = true;
}

} // namespace tiracon
