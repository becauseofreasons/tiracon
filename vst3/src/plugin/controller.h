#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "bridge.h"
#include <string>

namespace tiracon {

class Controller : public Steinberg::Vst::EditControllerEx1, public Steinberg::Vst::IMidiMapping {
public:
  static Steinberg::FUnknown* createInstance(void*) { return (Steinberg::Vst::IEditController*)new Controller; }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
  // IMidiMapping: CC number = firmware parameter number (as on the device), pitch bend
  Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                            Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                            Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

  // for the editor
  UiBridge* bridge() const { return uiBridge; }
  void editFirmwareParam(int no, int value, int phase);
  void sendRam(const uint8_t* ram2k);
  void sendDevice(const std::string& serial);
  void sendSettingsChanged();

  OBJ_METHODS(Controller, EditControllerEx1)
  DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
  END_DEFINE_INTERFACES(EditControllerEx1)
  REFCOUNT_METHODS(EditControllerEx1)

private:
  UiBridge* uiBridge = nullptr;
};

} // namespace tiracon
