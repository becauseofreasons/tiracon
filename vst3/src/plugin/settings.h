// Settings shared by all instances (per user): the standard RAM file.
//   Windows: %APPDATA%\Tiracon\settings.txt   Linux: ~/.config/Tiracon/settings.txt
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tiracon {

struct Settings {
  std::string ramDefault;          // path of the standard RAM file, empty = none
  bool autoWrite = true;           // programs saved with WRITE go into that file

  static Settings load();
  bool save() const;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out, size_t maxSize = 1 << 20);
bool writeFileAtomic(const std::string& path, const uint8_t* data, size_t n);
bool loadRamFile(const std::string& path, uint8_t ram[2048]);     // exactly 2048 bytes
std::string fileName(const std::string& path);

} // namespace tiracon
