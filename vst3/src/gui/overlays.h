// Overlays of the editor: parameter sheet (like the printed sheet of the device) and EG editor
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/events.h"
#include "uimodel.h"
#include "../plugin/bridge.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tiracon {

class SheetView : public VSTGUI::CView {
public:
  SheetView(const VSTGUI::CRect& r, std::function<void(int)> onSelect, std::function<void()> onClose)
    : CView(r), onSelect(std::move(onSelect)), onClose(std::move(onClose)) {}
  void setCurrent(int param, int value) { if (param != cur || value != curVal) { cur = param; curVal = value; invalid(); } }
  void setMessage(const std::string& m) { if (m != msg) { msg = m; invalid(); } }
  void draw(VSTGUI::CDrawContext* c) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
private:
  std::function<void(int)> onSelect;
  std::function<void()> onClose;
  int cur = -1, curVal = -1;
  std::string msg;
  std::map<int, VSTGUI::CRect> cells;
  VSTGUI::CRect closeRect;
  void layout();
};

// what the EG editor needs from outside
struct EgHost {
  std::function<void(int no, int value, int phase)> editParam;     // phase 0 begin, 1 move, 2 end
  std::function<void(bool on)> testTone;
  std::function<void()> close;
};

class EgView : public VSTGUI::CView {
public:
  EgView(const VSTGUI::CRect& r, EgHost host) : CView(r), host(std::move(host)) {}
  void update(const UiSnapshot& s);            // ~60x per second
  void reset();                                 // when opened
  void draw(VSTGUI::CDrawContext* c) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
  void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
private:
  EgHost host;
  bool filter = false;
  int vals[2][8] = {};                 // [vca/filter][R1 L1 R2 L2 R3 L3 R4 L4]
  double localUntil[2][8] = {};        // emulated time until which a local edit wins over the RAM
  double now = 0, view = 1;
  int prog = -1;
  int prevState[2][6];
  struct Live { int voice = -1; double t0 = 0, rel = -1; bool done = true; std::vector<std::pair<double, double>> pts; } live[2];
  enum Drag { None, H_R1, H_R2, H_R3, H_R4, H_S, Slider, Test } drag = None;
  int dragSlider = -1, dragStartVal = 0;
  VSTGUI::CRect graph, closeRect, tabRect[2], testRect, sliderRect[8];

  void layout();
  t6v::EgParams params(bool filt) const;
  void setVal(int k, int v);
  void track(const UiSnapshot& s);
  double holdTime(const t6v::EgParams& p) const;
  double xOf(double t) const;
  double yOf(double l) const;
};

} // namespace tiracon
