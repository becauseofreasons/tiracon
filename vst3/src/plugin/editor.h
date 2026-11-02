#pragma once
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "../gui/mainview.h"

namespace tiracon {

class Controller;

class Editor : public Steinberg::Vst::VSTGUIEditor, public EditorHost {
public:
  explicit Editor(Controller* c);
  bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
  void PLUGIN_API close() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;

  // EditorHost
  bool snapshot(UiSnapshot& s) override;
  void key(int code, bool down) override;
  void note(int pitch, bool on) override;
  void editParam(int no, int value, int phase) override;
  void newDevice() override;
  void showRamMenu(VSTGUI::CFrame* frame, const VSTGUI::CPoint& where) override;
  void setInstances(int n) override;

private:
  Controller* ctl;
  MainView* main = nullptr;
  VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;
  void push(UiCommand::Type t, int a, int b);
};

} // namespace tiracon
