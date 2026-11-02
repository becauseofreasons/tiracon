#include "mainview.h"
#include "views.h"
#include "overlays.h"
#include "theme.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include <algorithm>

using namespace VSTGUI;

namespace tiracon {

namespace {
constexpr double MIN_PRESS_MS = 130;
constexpr double LX = 12, LW = 516, RX = 540, RW = 308;

// background with the title
class Backdrop : public CView {
public:
  explicit Backdrop(const CRect& r) : CView(r) {}
  std::string serial;
  void draw(CDrawContext* c) override {
    c->setFillColor(theme::bg);
    c->drawRect(getViewSize(), kDrawFilled);
    CRect r = getViewSize();
    theme::label(c, "TIRACON", CRect(r.left + 12, r.top + 6, r.left + 120, r.top + 26), theme::accent, 14, true);
    theme::label(c, "6V", CRect(r.left + 92, r.top + 6, r.left + 200, r.top + 26), theme::dim, 10.5);
    if (!serial.empty()) theme::label(c, "Gerät " + serial, CRect(r.left + 170, r.top + 6, r.left + 300, r.top + 26), theme::dim, 10.5);
    setDirty(false);
  }
};
} // namespace

MainView::MainView(EditorHost& h) : CViewContainer(CRect(0, 0, W, H)), host(h) {
  setBackgroundColor(theme::bg);
  auto* back = new Backdrop(CRect(0, 0, W, H));
  back->setMouseEnabled(false);
  addView(back);
  // header buttons (right aligned)
  double x = W - 12;
  auto button = [&](double w, const char* label, std::function<void()> f) {
    x -= w;
    auto* b = new FlatButton(CRect(x, 6, x + w, 28), label, std::move(f));
    x -= 6;
    addView(b);
    return b;
  };
  btnDevice = button(110, "Neues Gerät", [this] { host.newDevice(); });
  btnRam = button(64, "RAM", [this] {
    if (auto* f = getFrame()) host.showRamMenu(f, CPoint(btnRam->getViewSize().left, btnRam->getViewSize().bottom));
  });
  btnEg = button(84, "EG-Editor", [this] { openEg(!eg->isVisible()); });
  btnSheet = button(104, "Parameterliste", [this] { openSheet(!sheet->isVisible()); });
  btnVoices = button(92, "6 Stimmen", [this] { showVoicesMenu(); });

  display = new DisplayView(CRect(LX, 36, LX + LW, 36 + 82));
  addView(display);
  info = new InfoView(CRect(LX, 124, LX + LW, 146));
  addView(info);
  keypad = new KeypadView(CRect(LX, 154, LX + LW, 154 + 128), [this](int code, bool down) { keyFromUi(code, down); });
  addView(keypad);
  slider = new ValueSlider(CRect(LX, 290, LX + LW, 314), [this](int v, int phase) {
    if (sliderParam >= 0) host.editParam(sliderParam, v, phase);
  });
  slider->setLabelWidth(190);
  addView(slider);
  voices = new VoicesView(CRect(RX, 36, RX + RW, 36 + 300));
  addView(voices);

  CRect ov(8, 6, W - 8, H - 6);
  sheet = new SheetView(ov, [this](int no) { selectParam(no); }, [this] { openSheet(false); });
  sheet->setVisible(false);
  addView(sheet);
  EgHost eh;
  eh.editParam = [this](int no, int v, int phase) { host.editParam(no, v, phase); };
  eh.testTone = [this](bool on) { host.note(60, on); };
  eh.close = [this] { openEg(false); };
  eg = new EgView(ov, eh);
  eg->reset();
  eg->setVisible(false);
  addView(eg);
}

void MainView::showVoicesMenu() {
  CFrame* f = getFrame();
  if (!f) return;
  CRect b = btnVoices->getViewSize();
  auto menu = makeOwned<COptionMenu>(CRect(b.left, b.bottom, b.left + 1, b.bottom + 1), nullptr, -1, nullptr, nullptr, COptionMenu::kPopupStyle);
  const int counts[4] = { 1, 2, 4, 8 };
  for (int n : counts)
    menu->addEntry(std::to_string(n * 6) + " Stimmen" + (n > 1 ? " (" + std::to_string(n) + " Tiracons)" : std::string()), -1,
                   n == snap.instances ? CMenuItem::kChecked : CMenuItem::kNoFlags);
  menu->popup(f, CPoint(b.left, b.bottom), [this, counts](COptionMenu* m) {
    int i = int(m->getLastResult());
    if (i >= 0 && i < 4) host.setInstances(counts[i]);
  });
}

void MainView::openSheet(bool on) {
  if (on) eg->setVisible(false);
  sheet->setMessage("");
  sheet->setVisible(on);
  btnSheet->setActive(on); btnEg->setActive(false);
  invalid();
}
void MainView::openEg(bool on) {
  if (on) { sheet->setVisible(false); eg->reset(); }
  eg->setVisible(on);
  btnEg->setActive(on); btnSheet->setActive(false);
  invalid();
}

void MainView::keyFromUi(int code, bool down) {
  if (down) {
    pressedAt[code] = clock;
    host.key(code, true);
    return;
  }
  double held = clock - pressedAt[code];
  if (held >= MIN_PRESS_MS) host.key(code, false);
  else { keypad->setPressed(code, true); releases.push_back({ code, pressedAt[code] + MIN_PRESS_MS }); }
}

void MainView::selectParam(int no) {
  if (!seq.empty()) return;
  const char* why = st.mode == t6v::DisplayState::Enable ? "ENABLE ist aktiv" : st.mode == t6v::DisplayState::Tune ? "AUTO TUNE läuft"
    : st.mode == t6v::DisplayState::Service ? "Servicemodus" : st.partial ? "Nummer halb eingegeben"
    : (st.mode == t6v::DisplayState::Prog && !t6v::isProgramNo(st.prog)) || (st.mode != t6v::DisplayState::Prog && st.mode != t6v::DisplayState::Param)
      ? "kein Programm- oder Parametermodus" : nullptr;
  if (why) { sheet->setMessage(std::string("Parameter ") + std::to_string(no) + " nicht wählbar: " + why); return; }
  openSheet(false);
  if (st.mode == t6v::DisplayState::Param && st.param == no) return;
  seq.clear();
  if (st.mode == t6v::DisplayState::Prog) { seq.push_back(t6v::KEY_PROG); seq.push_back(-1); }     // -1: wait for parameter mode
  seq.push_back(t6v::DIGIT_KEYS[no / 10]);
  seq.push_back(t6v::DIGIT_KEYS[no % 10]);
  seqPos = 0; seqNext = clock; seqDown = false; seqWait = 0;
}

void MainView::runSequence() {
  if (seq.empty() || clock < seqNext) return;
  if (seqPos >= seq.size()) { seq.clear(); return; }
  int code = seq[seqPos];
  if (code == -1) {                        // after PROG/PARAMETER: parameter mode without half entered number
    if (st.mode == t6v::DisplayState::Param && !st.partial) { seqPos++; return; }
    seqWait += 30;
    if (seqWait > 600) seq.clear();
    seqNext = clock + 30;
    return;
  }
  if (!seqDown) { host.key(code, true); keypad->setPressed(code, true); seqDown = true; seqNext = clock + 140; }
  else { host.key(code, false); keypad->setPressed(code, false); seqDown = false; seqNext = clock + 170; seqPos++; }
}

void MainView::updateInfo() {
  int from = -1, to = -1;
  std::string no, title;
  std::vector<std::string> vals;
  int cur = -1;
  sliderParam = -1;
  switch (st.mode) {
    case t6v::DisplayState::Param: {
      const t6v::ParamInfo* p = st.partial ? nullptr : t6v::paramInfo(st.param);
      if (st.partial) { title = "Parameter " + std::to_string(st.param) + "_"; break; }
      if (!p) { no = std::to_string(st.param); title = "-"; break; }
      no = std::to_string(p->no); title = p->title; from = 0; to = p->max;
      for (int v = 0; v <= p->max && v < int(p->vals.size()); v++) vals.push_back(p->vals[size_t(v)]);
      cur = st.value;
      if (p->no != 88 && t6v::romInfo().cc) sliderParam = p->no;
      break;
    }
    case t6v::DisplayState::Prog:
      title = st.partial ? "Programm " + std::to_string(st.prog) + "_" : "Programm " + std::to_string(st.prog);
      if (!st.partial) { from = 11; to = 48; }
      break;
    case t6v::DisplayState::Enable: title = "ENABLE - Programm speichern"; break;
    case t6v::DisplayState::Tune: title = "AUTO TUNE"; break;
    case t6v::DisplayState::Service: title = "Servicemodus"; break;
    default: break;
  }
  info->set(no, title, vals, cur);
  display->setData(smooth, from, to);
  if (sliderParam >= 0) {
    const t6v::ParamInfo* p = t6v::paramInfo(sliderParam);
    slider->set(std::to_string(p->no) + "  " + p->label, "", std::max(0, st.value), p->max, true);
  } else {
    slider->set("", "", 0, 1, false);
  }
  sheet->setCurrent(st.mode == t6v::DisplayState::Param && !st.partial ? st.param : -1, st.value);
}

void MainView::apply(const UiSnapshot& s) {
  snap = s;
  for (int i = 0; i < 64; i++) smooth[i] = smooth[i] * 0.45f + s.disp[i] * 0.55f;
  // the display state counts once it was read twice in a row (multiplex in between)
  t6v::DisplayState now = t6v::readDisplay(s.disp);
  if (now == pend) pendCount++; else { pend = now; pendCount = 1; }
  if (pendCount >= 2) st = pend;
  updateInfo();
  voices->setData(s);
  btnVoices->setLabel(std::to_string(std::max(1, s.instances) * 6) + " Stimmen");
  if (eg->isVisible()) eg->update(s);
  auto* back = static_cast<Backdrop*>(getView(0));
  if (back->serial != s.serial) { back->serial = s.serial; back->invalid(); }
}

void MainView::tick(double ms) {
  clock += ms;
  for (size_t i = 0; i < releases.size();) {
    if (clock >= releases[i].at) { host.key(releases[i].code, false); keypad->setPressed(releases[i].code, false); releases.erase(releases.begin() + long(i)); }
    else i++;
  }
  runSequence();
  if (host.snapshot(snap) && snap.seq != lastSeq) {
    lastSeq = snap.seq;
    apply(snap);
  }
}

} // namespace tiracon
