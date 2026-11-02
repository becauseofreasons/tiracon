#include "settings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace tiracon {

namespace {
#ifdef _WIN32
std::wstring widen(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}
FILE* openFile(const std::string& p, const char* mode) {
  std::wstring m(mode, mode + std::strlen(mode));
  return _wfopen(widen(p).c_str(), m.c_str());
}
std::string configDir() {
  const wchar_t* a = _wgetenv(L"APPDATA");
  if (!a) return std::string();
  std::wstring d = std::wstring(a) + L"\\Tiracon";
  // versions before 1.0 used the folder "Tirakon": take it over once
  std::wstring old = std::wstring(a) + L"\\Tirakon";
  if (GetFileAttributesW(d.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES)
    MoveFileW(old.c_str(), d.c_str());
  _wmkdir(d.c_str());
  int n = WideCharToMultiByte(CP_UTF8, 0, d.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, d.c_str(), -1, &s[0], n, nullptr, nullptr);
  return s;
}
bool renameFile(const std::string& a, const std::string& b) {
  return MoveFileExW(widen(a).c_str(), widen(b).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
#else
FILE* openFile(const std::string& p, const char* mode) { return std::fopen(p.c_str(), mode); }
std::string configDir() {
  std::string base;
  if (const char* x = std::getenv("XDG_CONFIG_HOME")) base = x;
  else if (const char* h = std::getenv("HOME")) base = std::string(h) + "/.config";
  else return std::string();
  mkdir(base.c_str(), 0755);
  std::string d = base + "/Tiracon";
  // versions before 1.0 used the folder "Tirakon": take it over once
  std::string old = base + "/Tirakon";
  struct stat st;
  if (stat(d.c_str(), &st) != 0 && stat(old.c_str(), &st) == 0) std::rename(old.c_str(), d.c_str());
  mkdir(d.c_str(), 0755);
  return d;
}
bool renameFile(const std::string& a, const std::string& b) { return std::rename(a.c_str(), b.c_str()) == 0; }
#endif
std::string settingsPath() {
  std::string d = configDir();
  if (d.empty()) return d;
#ifdef _WIN32
  return d + "\\settings.txt";
#else
  return d + "/settings.txt";
#endif
}
} // namespace

bool readFile(const std::string& path, std::vector<uint8_t>& out, size_t maxSize) {
  FILE* f = openFile(path, "rb");
  if (!f) return false;
  out.clear();
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
    out.insert(out.end(), buf, buf + n);
    if (out.size() > maxSize) { std::fclose(f); return false; }
  }
  std::fclose(f);
  return true;
}

bool writeFileAtomic(const std::string& path, const uint8_t* data, size_t n) {
  std::string tmp = path + ".tmp";
  FILE* f = openFile(tmp, "wb");
  if (!f) return false;
  bool ok = std::fwrite(data, 1, n, f) == n;
  ok = std::fclose(f) == 0 && ok;
  if (!ok) { std::remove(tmp.c_str()); return false; }
  return renameFile(tmp, path);
}

bool loadRamFile(const std::string& path, uint8_t ram[2048]) {
  std::vector<uint8_t> v;
  if (!readFile(path, v, 4096) || v.size() != 2048) return false;
  std::memcpy(ram, v.data(), 2048);
  return true;
}

std::string fileName(const std::string& path) {
  size_t p = path.find_last_of("/\\");
  return p == std::string::npos ? path : path.substr(p + 1);
}

Settings Settings::load() {
  Settings s;
  std::vector<uint8_t> v;
  std::string p = settingsPath();
  if (p.empty() || !readFile(p, v, 65536)) return s;
  std::istringstream in(std::string(v.begin(), v.end()));
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), val = line.substr(eq + 1);
    if (k == "ramDefault") s.ramDefault = val;
    else if (k == "autoWrite") s.autoWrite = val == "1";
  }
  return s;
}

bool Settings::save() const {
  std::string p = settingsPath();
  if (p.empty()) return false;
  std::string t = "ramDefault=" + ramDefault + "\nautoWrite=" + (autoWrite ? "1" : "0") + "\n";
  return writeFileAtomic(p, reinterpret_cast<const uint8_t*>(t.data()), t.size());
}

} // namespace tiracon
