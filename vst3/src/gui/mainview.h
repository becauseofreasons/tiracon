// The whole editor content. Talks to the outside only through EditorHost, so it also runs
// without the plugin (test/uishot.cpp renders it into a PNG).
#pragma once
#include "vstgui/lib/cviewcontainer.h"
#include "../plugin/bridge.h"
#include "uimodel.h"
#include <functional>
#include <string>
#include <vector>

namespace tiracon {

class DisplayView; class InfoView; class KeypadView; class ValueSlider; class VoicesView; class FlatButton;
class SheetView; class EgView;

struct EditorHost {
  virtual ~EditorHost() = default;
  virtual bool snapshot(UiSnapshot& s) = 0;
  virtual void key(int code, bool down) = 0;                 // BLP key
  virtual void note(int pitch, bool on) = 0;                 // test tone
  virtual void editParam(int no, int value, int phase) = 0;  // phase 0 begin, 1 perform, 2 end
  virtual void newDevice() = 0;
  virtual void showRamMenu(VSTGUI::CFrame* frame, const VSTGUI::CPoint& where) = 0;
  virtual void setInstances(int n) = 0;                      // 1, 2, 4, 8 Tiracons
};

class MainView : public VSTGUI::CViewContainer {
public:
  static constexpr double W = 860, H = 346;
  explicit MainView(EditorHost& host);
  void tick(double ms);                   // timer, ~30 ms
  void apply(const UiSnapshot& s);        // also used directly by the screenshot tool
  void openSheet(bool on);
  void openEg(bool on);

private:
  EditorHost& host;
  DisplayView* display = nullptr;
  InfoView* info = nullptr;
  KeypadView* keypad = nullptr;
  ValueSlider* slider = nullptr;
  VoicesView* voices = nullptr;
  FlatButton* btnSheet = nullptr; FlatButton* btnEg = nullptr; FlatButton* btnRam = nullptr; FlatButton* btnDevice = nullptr; FlatButton* btnVoices = nullptr;
  SheetView* sheet = nullptr;
  EgView* eg = nullptr;
  UiSnapshot snap;
  uint32_t lastSeq = 0;
  float smooth[64] = {};
  t6v::DisplayState st, pend;
  int pendCount = 0;
  double clock = 0;                        // editor time in ms
  // keys: minimum press time like the WebApp (keypad decoded every ~56 ms)
  double pressedAt[32] = {};
  struct Release { int code; double at; };
  std::vector<Release> releases;
  // parameter selection from the sheet: PROG/PARAMETER (if needed) + two digits
  std::vector<int> seq;
  size_t seqPos = 0;
  double seqNext = 0, seqWait = 0;
  bool seqDown = false;
  int sliderParam = -1;

  void keyFromUi(int code, bool down);
  void selectParam(int no);
  void runSequence();
  void updateInfo();
  void showVoicesMenu();
};

} // namespace tiracon
