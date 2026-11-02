// Basic views of the editor. All drawing in code, no bitmaps.
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/events.h"
#include "../plugin/bridge.h"
#include <functional>
#include <string>
#include <vector>

namespace tiracon {

// flat text button
class FlatButton : public VSTGUI::CView {
public:
  FlatButton(const VSTGUI::CRect& r, std::string label, std::function<void()> onClick)
    : CView(r), label(std::move(label)), onClick(std::move(onClick)) {}
  void setLabel(const std::string& l) { if (l != label) { label = l; invalid(); } }
  void setActive(bool a) { if (a != active) { active = a; invalid(); } }
  void setPressedHandler(std::function<void(bool)> f) { onPress = std::move(f); }   // hold-to-play buttons
  void draw(VSTGUI::CDrawContext* c) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
private:
  std::string label;
  std::function<void()> onClick;
  std::function<void(bool)> onPress;
  bool down = false, active = false;
};

// 7-segment display: program, LEDs, parameter, value, range (von / bis)
class DisplayView : public VSTGUI::CView {
public:
  explicit DisplayView(const VSTGUI::CRect& r) : CView(r) {}
  void setData(const float* d64, int rangeFrom, int rangeTo);       // range -1 = blank
  void draw(VSTGUI::CDrawContext* c) override;
private:
  float d[64] = {};
  int from = -1, to = -1;
  void digit(VSTGUI::CDrawContext* c, double x, double y, double w, double h, int seg, const float* lit);
};

// short info line under the display: "71  LOW PASS CUTOFF" and the value names
class InfoView : public VSTGUI::CView {
public:
  explicit InfoView(const VSTGUI::CRect& r) : CView(r) {}
  void set(const std::string& no, const std::string& title, const std::vector<std::string>& vals, int current);
  void draw(VSTGUI::CDrawContext* c) override;
private:
  std::string no, title;
  std::vector<std::string> vals;
  int current = -1;
};

// BLP keypad, 4 x 8 like the device
class KeypadView : public VSTGUI::CView {
public:
  KeypadView(const VSTGUI::CRect& r, std::function<void(int, bool)> onKey) : CView(r), onKey(std::move(onKey)) {}
  void setPressed(int code, bool down);
  void draw(VSTGUI::CDrawContext* c) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
private:
  std::function<void(int, bool)> onKey;
  uint32_t pressed = 0;
  int held = -1;
  int codeAt(const VSTGUI::CPoint& p) const;
  VSTGUI::CRect cell(int row, int col) const;
};

// horizontal slider with label and value (used for the displayed parameter and in the EG editor)
class ValueSlider : public VSTGUI::CView {
public:
  ValueSlider(const VSTGUI::CRect& r, std::function<void(int value, int phase)> onEdit)   // phase 0 begin 1 move 2 end
    : CView(r), onEdit(std::move(onEdit)) {}
  void set(const std::string& label, const std::string& detail, int value, int max, bool enabled);
  bool dragging() const { return drag; }
  void setLabelWidth(double w) { labelW = w; }
  void draw(VSTGUI::CDrawContext* c) override;
  void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
  void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
  void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
  void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
private:
  std::function<void(int, int)> onEdit;
  std::string label, detail;
  int value = 0, max = 1;
  bool enabled = false, drag = false;
  double labelW = 150;
  VSTGUI::CRect track() const;
  int valueAt(double x) const;
};

// voices (note, VCO, VCA, VCF) and the control voltages of the ALP
class VoicesView : public VSTGUI::CView {
public:
  explicit VoicesView(const VSTGUI::CRect& r) : CView(r) {}
  void setData(const UiSnapshot& s) { snap = s; invalid(); }
  void draw(VSTGUI::CDrawContext* c) override;
private:
  UiSnapshot snap;
};

} // namespace tiracon
