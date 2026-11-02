// Component tolerances of one Tiracon 6V ("device" = serial number as random seed) -
// C++ port of emulator/js/tolerance.js. Allocates: build it outside the audio thread.
#pragma once
#include "board.h"
#include "synth.h"
#include <string>

namespace t6v {

struct DeviceProfile {
  std::string serial;
  BoardProfile board;
  SynthProfile synth;
};

// serial: any text (the WebApp uses digits, default "1266"); ideal = no tolerances
DeviceProfile makeDevice(const std::string& serial, bool ideal = false);

} // namespace t6v
