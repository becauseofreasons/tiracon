#include "editor.h"
#include <cctype>
#include "controller.h"
#include "linux_runloop.h"
#include "settings.h"
#include "firmware.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include <cstring>
#include <random>

using namespace VSTGUI;
using namespace Steinberg;

namespace tiracon {

Editor::Editor(Controller* c) : VSTGUIEditor(c), ctl(c) {
  ViewRect r(0, 0, int32(MainView::W), int32(MainView::H));
  setRect(r);
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
  if (frame) return false;
  frame = new CFrame(CRect(0, 0, MainView::W, MainView::H), this);
  frame->setTransparency(false);
  main = new MainView(*this);
  frame->addView(main);
  bool ok;
#if SMTG_OS_LINUX
  VSTGUI::X11::FrameConfig config;
  config.runLoop = makeOwned<HostRunLoop>(plugFrame);
  ok = frame->open(parent, platformType, &config);
#else
  ok = frame->open(parent, platformType);
#endif
  if (!ok) { frame->forget(); frame = nullptr; main = nullptr; return false; }
  timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { if (main) main->tick(30); }, 30, true);
  return true;
}

void PLUGIN_API Editor::close() {
  if (timer) { timer->stop(); timer = nullptr; }
  if (frame) {
    frame->close();          // releases the views
    frame = nullptr;
  }
  main = nullptr;
}

tresult PLUGIN_API Editor::isPlatformTypeSupported(FIDString type) {
#if SMTG_OS_LINUX
  if (strcmp(type, kPlatformTypeX11EmbedWindowID) != 0) return kInvalidArgument;     // X11 only (also under Xwayland)
#endif
  return VSTGUIEditor::isPlatformTypeSupported(type);
}

bool Editor::snapshot(UiSnapshot& s) {
  UiBridge* b = ctl->bridge();
  if (!b) return false;
  b->read(s);
  return s.seq != 0;
}
void Editor::push(UiCommand::Type t, int a, int b) {
  if (UiBridge* br = ctl->bridge()) br->push(UiCommand{ t, uint8_t(a), uint8_t(b) });
}
void Editor::key(int code, bool down) { push(UiCommand::BlpKey, code, down ? 1 : 0); }
void Editor::note(int pitch, bool on) { push(UiCommand::Note, pitch, on ? 1 : 0); }

void Editor::editParam(int no, int value, int phase) {
  if (no >= 11 && no <= 76) { ctl->editFirmwareParam(no, value, phase); return; }
  if (phase == 1) push(UiCommand::Param, no, value);      // global MIDI parameters: straight to the firmware
}

void Editor::setInstances(int n) { push(UiCommand::Instances, n, 0); }

void Editor::newDevice() {
  std::random_device rd;
  ctl->sendDevice(std::to_string(1000 + rd() % 9000));
}

void Editor::showRamMenu(CFrame* f, const CPoint& where) {
  Settings s = Settings::load();
  auto menu = makeOwned<COptionMenu>(CRect(where.x, where.y, where.x + 1, where.y + 1), nullptr, -1, nullptr, nullptr, COptionMenu::kPopupStyle);
  enum { LOAD, SAVE, SEP1, STD_TITLE, STD_CHOOSE, STD_SAVE, STD_LOAD, STD_CLEAR, STD_AUTO, SEP2, FACTORY };
  menu->addEntry("RAM laden ...");
  menu->addEntry("RAM speichern ...");
  menu->addSeparator();
  menu->addEntry("Standard: " + UTF8String(s.ramDefault.empty() ? std::string("keine Datei") : fileName(s.ramDefault)), -1, CMenuItem::kDisabled);
  menu->addEntry("Datei als Standard wählen ...");
  menu->addEntry("Aktuelles RAM als Standard speichern ...");
  menu->addEntry("Standard jetzt laden", -1, s.ramDefault.empty() ? CMenuItem::kDisabled : CMenuItem::kNoFlags);
  menu->addEntry("Standard entfernen", -1, s.ramDefault.empty() ? CMenuItem::kDisabled : CMenuItem::kNoFlags);
  menu->addEntry("WRITE in Standard-Datei übernehmen", -1,
                 (s.autoWrite ? CMenuItem::kChecked : 0) | (s.ramDefault.empty() ? CMenuItem::kDisabled : 0));
  menu->addSeparator();
  menu->addEntry("Werksprogramme laden");

  Controller* c = ctl;
  auto pickFile = [f](CNewFileSelector::Style style, const char* title, std::function<void(std::string)> done) {
    auto sel = VSTGUI::owned(CNewFileSelector::create(f, style));
    if (!sel) return;
    sel->setTitle(title);
    sel->addFileExtension(CFileExtension("RAM-Datei", "bin"));
    if (style == CNewFileSelector::kSelectSaveFile) sel->setDefaultSaveName("tiracon_ram.bin");
    bool save = style == CNewFileSelector::kSelectSaveFile;
    sel->run([done, save](CNewFileSelector* fs) {
      if (fs->getNumSelectedFiles() == 0) return;
      std::string p = fs->getSelectedFile(0);
      // the dialog does not add the extension itself on every system
      auto lower = [](std::string x) { for (auto& ch : x) ch = char(std::tolower(static_cast<unsigned char>(ch))); return x; };
      if (save && (p.size() < 4 || lower(p.substr(p.size() - 4)) != ".bin")) p += ".bin";
      done(p);
    });
  };
  menu->popup(f, where, [c, s, pickFile](COptionMenu* m) {
    Settings st = s;
    switch (m->getLastResult()) {
      case LOAD:
        pickFile(CNewFileSelector::kSelectFile, "RAM laden", [c](std::string p) {
          uint8_t ram[2048];
          if (loadRamFile(p, ram)) c->sendRam(ram);
        });
        break;
      case SAVE:
        pickFile(CNewFileSelector::kSelectSaveFile, "RAM speichern", [c](std::string p) {
          UiSnapshot snap;
          if (c->bridge()) { c->bridge()->read(snap); writeFileAtomic(p, snap.ram, 2048); }
        });
        break;
      case STD_CHOOSE:
        pickFile(CNewFileSelector::kSelectFile, "Standard-RAM wählen", [c, st](std::string p) {
          uint8_t ram[2048];
          if (!loadRamFile(p, ram)) return;
          Settings x = st; x.ramDefault = p; x.save();
          c->sendSettingsChanged();
        });
        break;
      case STD_SAVE:
        pickFile(CNewFileSelector::kSelectSaveFile, "Aktuelles RAM als Standard speichern", [c, st](std::string p) {
          UiSnapshot snap;
          if (!c->bridge()) return;
          c->bridge()->read(snap);
          if (!writeFileAtomic(p, snap.ram, 2048)) return;
          Settings x = st; x.ramDefault = p; x.save();
          c->sendSettingsChanged();
        });
        break;
      case STD_LOAD: {
        uint8_t ram[2048];
        if (loadRamFile(st.ramDefault, ram)) c->sendRam(ram);
        break;
      }
      case STD_CLEAR: st.ramDefault.clear(); st.save(); c->sendSettingsChanged(); break;
      case STD_AUTO: st.autoWrite = !st.autoWrite; st.save(); c->sendSettingsChanged(); break;
      case FACTORY: c->sendRam(t6v::factoryRam()); break;
      default: break;
    }
  });
}

} // namespace tiracon
