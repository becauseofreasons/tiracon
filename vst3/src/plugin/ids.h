#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace tiracon {

// class ids - never change them, DAW projects refer to them
static const Steinberg::FUID ProcessorUID(0x5A7C3E21, 0x8B4F4D19, 0xA62E7F03, 0x1D9B6C45);
static const Steinberg::FUID ControllerUID(0x3F18A6D2, 0x47C94E8B, 0x9D05B2E7, 0x6A3C1F80);

// parameter ids: 11..76 = firmware parameter number (= MIDI CC number of the firmware)
enum : Steinberg::Vst::ParamID {
  kParamVolume = 200,
  kParamPitchBend = 201,
  kParamProgram = 202,          // also the id of the program list
};

constexpr const char* kVendor = "BecauseOfReasons";
constexpr const char* kPluginName = "Tiracon";
constexpr const char* kVersion = "1.0.0";

// processor state (little endian): "TRKN", u32 version, 2048 bytes RAM, u32 length + serial, f64 volume, u32 instances
constexpr Steinberg::uint32 kStateMagic = 0x4E4B5254;   // "TRKN"
constexpr Steinberg::uint32 kStateVersion = 2;      // 2: + u32 number of instances

} // namespace tiracon
