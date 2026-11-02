// Processor state: read / write (shared by processor and controller)
#pragma once
#include "ids.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include <cstdint>
#include <string>

namespace tiracon {

struct PluginState {
  uint8_t ram[2048] = {};
  std::string serial = "1266";
  double volume = 0.7;
  uint32_t instances = 1;          // 1, 2, 4, 8 Tiracons (6..48 voices)

  bool read(Steinberg::IBStream* stream) {
    Steinberg::IBStreamer s(stream, kLittleEndian);
    Steinberg::uint32 magic = 0, version = 0, len = 0;
    if (!s.readInt32u(magic) || magic != kStateMagic) return false;
    if (!s.readInt32u(version) || version < 1) return false;
    if (s.readRaw(ram, sizeof ram) != sizeof ram) return false;
    if (!s.readInt32u(len) || len > 64) return false;
    std::string t(len, '\0');
    if (len && s.readRaw(&t[0], len) != Steinberg::TSize(len)) return false;
    serial = t.empty() ? "1266" : t;
    double v = 0.7;
    if (s.readDouble(v)) volume = v;
    Steinberg::uint32 n = 1;
    if (version >= 2 && s.readInt32u(n)) instances = n >= 1 && n <= 8 ? n : 1;
    return true;
  }
  bool write(Steinberg::IBStream* stream) const {
    Steinberg::IBStreamer s(stream, kLittleEndian);
    return s.writeInt32u(kStateMagic) && s.writeInt32u(kStateVersion) &&
           s.writeRaw(ram, sizeof ram) == sizeof ram &&
           s.writeInt32u(Steinberg::uint32(serial.size())) &&
           s.writeRaw(serial.data(), Steinberg::int32(serial.size())) == Steinberg::TSize(serial.size()) &&
           s.writeDouble(volume) && s.writeInt32u(instances);
  }
};

} // namespace tiracon
