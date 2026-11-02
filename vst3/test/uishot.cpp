// Renders the editor without a window into PNG files, with real data from the engine.
//   tiracon_uishot <output folder> [scale]
#include "../src/gui/mainview.h"
#include "../src/plugin/snapshot.h"
#include "multiengine.h"
#include "uimodel.h"
#include "vstgui/lib/vstguiinit.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>
#include <string>
#include <vector>

using namespace VSTGUI;
using namespace tiracon;

struct StubHost : EditorHost {
  t6v::MultiEngine* e = nullptr;
  UiSnapshot last;
  bool snapshot(UiSnapshot& s) override { s = last; return true; }
  void key(int code, bool down) override { e->key(code, down); }
  void note(int pitch, bool on) override { e->testNote(pitch, on); }
  void editParam(int no, int value, int phase) override { if (phase == 1) e->hostParam(no, value); }
  void newDevice() override {}
  void showRamMenu(CFrame*, const CPoint&) override {}
  void setInstances(int n) override { e->setCount(n); }
};

static double gScale = 1.0;
static void render(MainView& v, const std::string& path) {
  // scale by a transform (a backing scale factor would only enlarge the canvas with Cairo)
  auto ctx = COffscreenContext::create(CPoint(MainView::W * gScale, MainView::H * gScale), 1.0);
  if (!ctx) { std::printf("kein Offscreen-Zeichnen auf diesem System (%s)\n", path.c_str()); return; }
  ctx->beginDraw();
  {
    CDrawContext::Transform t(*ctx, CGraphicsTransform().scale(gScale, gScale));
    v.drawRect(ctx, CRect(0, 0, MainView::W, MainView::H));
  }
  ctx->endDraw();
  auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx->getBitmap()->getPlatformBitmap());
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
  std::printf("geschrieben: %s (%zu Byte)\n", path.c_str(), png.size());
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  if (argc > 2) gScale = std::atof(argv[2]);            // e.g. 2 for a sharper picture
#ifdef _WIN32
  VSTGUI::init(GetModuleHandle(nullptr));
#else
  VSTGUI::init(nullptr);                     // no plugin bundle: no resource path needed
#endif
  t6v::MultiEngine e;
  e.prepare(48000, 512);
  StubHost host;
  host.e = &e;
  std::vector<float> buf(480);
  auto run = [&](double s) { for (int i = 0; i < int(s * 100); i++) e.render(buf.data(), 480); };
  auto press = [&](int code) { e.key(code, true); run(0.14); e.key(code, false); run(0.17); };
  run(2.0);
  {
    auto* view = new MainView(host);
    auto snap = [&] { fillSnapshot(e, host.last, e.serial()); host.last.seq++; view->apply(host.last); };
    // parameter 13 PULSE 4' on the display, a chord playing
    press(t6v::KEY_PROG); press(t6v::DIGIT_KEYS[1]); press(t6v::DIGIT_KEYS[3]);
    e.noteOn(60, 0.8f); e.noteOn(64, 0.6f); e.noteOn(67, 0.9f);
    for (int i = 0; i < 8; i++) { run(0.03); snap(); }
    if (std::getenv("UISHOT_DEBUG")) {
      for (int dig = 0; dig < 7; dig++) { std::printf("d%d:", dig); for (int b = 7; b >= 0; b--) std::printf(" %.2f", host.last.disp[dig * 8 + b]); std::printf("\n"); }
    }
    render(*view, out + "/ui_main.png");
    view->openSheet(true);
    render(*view, out + "/ui_sheet.png");
    // EG editor with the live trace of a new note
    e.noteOff(60); e.noteOff(64); e.noteOff(67);
    run(1.5);
    view->openEg(true);
    snap();
    e.noteOn(62, 0.8f);
    for (int i = 0; i < 40; i++) { run(0.02); snap(); }
    e.noteOff(62);
    for (int i = 0; i < 40; i++) { run(0.02); snap(); }
    render(*view, out + "/ui_eg.png");
    // 24 voices: 4 Tiracons, a big chord
    view->openEg(false);
    e.setCount(4);
    run(2.5);
    for (int p = 48; p < 66; p++) { e.noteOn(p, 0.8f); run(0.01); }
    for (int i = 0; i < 10; i++) { run(0.03); snap(); }
    render(*view, out + "/ui_24.png");
    view->forget();
  }
  VSTGUI::exit();
  return 0;
}
