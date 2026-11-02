#include "controller.h"
#include "ids.h"
#include "state.h"
#include "engine.h"
#include "uimodel.h"
#include "editor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstring.h"
#include <cstring>
#include "pluginterfaces/base/ustring.h"
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace tiracon {

namespace {
void toU16(const std::string& s, String128 out) {
  UString(out, 128).fromAscii(s.c_str());
}
}

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
  tresult r = EditControllerEx1::initialize(context);
  if (r != kResultOk) return r;

  // program list 11..48 (the RAM stores no names)
  addUnit(new Unit(STR16("Tiracon"), kRootUnitId, kNoParentUnitId, kParamProgram));
  auto* list = new ProgramList(STR16("Programme"), kParamProgram, kRootUnitId);
  for (int i = 0; i < t6v::Engine::PROGRAMS; i++) {
    String128 name;
    toU16(std::to_string((i / 8 + 1) * 10 + i % 8 + 1), name);
    list->addProgram(name);
  }
  addProgramList(list);
  parameters.addParameter(list->getParameter());

  parameters.addParameter(new RangeParameter(STR16("Volume"), kParamVolume, STR16("%"), 0, 100, 70, 0, ParameterInfo::kCanAutomate));
  parameters.addParameter(STR16("Pitch Bend"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kParamPitchBend);

  // firmware parameters, id = parameter number
  for (const t6v::ParamDef& p : t6v::firmwareParams()) {
    String128 title, shortTitle;
    toU16(std::to_string(p.no) + " " + p.info->title, title);
    toU16(std::to_string(p.no), shortTitle);
    if (!p.info->vals.empty()) {
      auto* sp = new StringListParameter(title, ParamID(p.no), nullptr, ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
      for (int v = 0; v <= p.max; v++) {
        String128 u;
        toU16(p.info->valueName(v), u);
        sp->appendString(u);
      }
      UString(sp->getInfo().shortTitle, 128).assign(shortTitle);
      parameters.addParameter(sp);
    } else {
      auto* rp = new RangeParameter(title, ParamID(p.no), nullptr, 0, p.max, 0, p.max, ParameterInfo::kCanAutomate);
      UString(rp->getInfo().shortTitle, 128).assign(shortTitle);
      parameters.addParameter(rp);
    }
  }
  return kResultOk;
}

// project loaded: parameter values from the RAM image (edit buffer), volume
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
  PluginState st;
  if (!st.read(stream)) return kResultFalse;
  setParamNormalized(kParamVolume, st.volume);
  const t6v::RomInfo& rom = t6v::romInfo();
  for (const t6v::ParamDef& p : t6v::firmwareParams()) {
    int v = t6v::ramValue(rom, st.ram, p.no);
    if (v >= 0 && v <= p.max) setParamNormalized(ParamID(p.no), p.max > 0 ? double(v) / p.max : 0);
  }
  int pn = t6v::bcd(st.ram[t6v::RAM_PROGNO]);
  int b = pn / 10, s = pn % 10;
  if (b >= 1 && b <= 4 && s >= 1 && s <= 8) setParamNormalized(kParamProgram, ((b - 1) * 8 + s - 1) / 31.0);
  return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
  if (uiBridge) { uiBridge->release(); uiBridge = nullptr; }
  return EditControllerEx1::terminate();
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
  if (FIDStringsEqual(name, ViewType::kEditor)) return new Editor(this);
  return nullptr;
}

// the processor hands over the bridge once (same process, see bridge.h)
tresult PLUGIN_API Controller::notify(IMessage* message) {
  if (!message) return kInvalidArgument;
  if (FIDStringsEqual(message->getMessageID(), "bridge")) {
    int64 ptr = 0;
    if (message->getAttributes()->getInt("ptr", ptr) == kResultOk && ptr) {
      UiBridge* b = reinterpret_cast<UiBridge*>(ptr);
      b->addRef();
      if (uiBridge) uiBridge->release();
      uiBridge = b;
    }
    return kResultOk;
  }
  return EditControllerEx1::notify(message);
}

void Controller::editFirmwareParam(int no, int value, int phase) {
  const t6v::ParamInfo* p = t6v::paramInfo(no);
  if (!p) return;
  ParamID id = ParamID(no);
  if (phase == 0) beginEdit(id);
  else if (phase == 1) {
    double v = p->max > 0 ? double(value) / p->max : 0;
    setParamNormalized(id, v);
    performEdit(id, v);
  } else endEdit(id);
}

void Controller::sendRam(const uint8_t* ram2k) {
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("ram");
    msg->getAttributes()->setBinary("data", ram2k, 2048);
    sendMessage(msg);
  }
}
void Controller::sendDevice(const std::string& serial) {
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("device");
    msg->getAttributes()->setBinary("serial", serial.data(), uint32(serial.size()));
    sendMessage(msg);
  }
}
void Controller::sendSettingsChanged() {
  if (auto msg = owned(allocateMessage())) {
    msg->setMessageID("settings");
    sendMessage(msg);
  }
}

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber cc, ParamID& id) {
  (void)channel;                      // the plugin forwards everything on the firmware's channel
  if (busIndex != 0) return kResultFalse;
  if (cc == kPitchBend) { id = kParamPitchBend; return kResultTrue; }
  if (cc >= 0 && cc < 100 && t6v::romInfo().has(cc) && cc >= 11 && cc <= 76) { id = ParamID(cc); return kResultTrue; }
  return kResultFalse;
}

} // namespace tiracon
