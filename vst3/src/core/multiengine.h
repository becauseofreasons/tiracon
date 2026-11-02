// Several complete Tiracons side by side (VST feature, not in the WebApp): 1, 2, 4 or 8 instances = 6, 12, 24
// or 48 voices. Every instance runs the unchanged firmware with its own components ("device").
// - notes: to the instance with the fewest held notes (ties: round robin); unison (P37) lowers the capacity
// - parameters, pitch bend, program changes and panel keys: to all instances
// - instance 1 is the one the editor shows; the others follow it (edit buffer, programs, program number)
// No allocation on the audio thread: all instances exist from the start, inactive ones do not run.
#pragma once
#include "engine.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace t6v {

class MultiEngine {
public:
  static constexpr int MAX = 8;
  static constexpr int64_t SYNC = CPU_HZ * 150 / 1000;     // compare the instances every 150 ms

  MultiEngine();
  void prepare(double sampleRate, int maxBlock);            // allocates: not on the audio thread
  static std::string serialOf(const std::string& base, int i) { return i == 0 ? base : base + "-" + std::to_string(i + 1); }
  static std::array<DeviceProfile, MAX> makeDevices(const std::string& base);   // allocates

  // audio thread
  void setCount(int n);                                    // 1, 2, 4, 8 (others are rounded)
  int count() const { return n; }
  void noteOn(int pitch, float velocity);
  void noteOff(int pitch);
  void hostParam(int no, int value) { for (int i = 0; i < n; i++) e[i]->hostParam(no, value); }
  void hostPitchBend(double v) { for (int i = 0; i < n; i++) e[i]->hostPitchBend(v); }
  void hostProgram(int index) { for (int i = 0; i < n; i++) e[i]->hostProgram(index); }
  void key(int code, bool down) { for (int i = 0; i < n; i++) e[i]->hw().setBlpKey(code, down); }
  void testNote(int pitch, bool on) { if (on) e[0]->noteOn(pitch, 0.8f); else e[0]->noteOff(pitch); }   // EG editor: instance 1
  void setVolume(double v) { volume = v; for (auto& x : e) x->setVolume(v); }
  void render(float* out, int samples);
  int takeChanges(Engine::Change* out, int max);           // from instance 1

  // state (caller makes sure the audio thread is not inside)
  void loadRam(const uint8_t* ram2k);                      // all instances, power on
  void setDevices(const std::array<DeviceProfile, MAX>& d);   // all instances, power on
  void powerOn();
  Engine& primary() { return *e[0]; }
  Engine& at(int i) { return *e[size_t(i)]; }
  const std::string& serial() const { return e[0]->serial(); }

private:
  std::array<std::unique_ptr<Engine>, MAX> e;
  int n = 1;
  double volume = 0.7;
  std::vector<float> scratch;
  int8_t noteInst[128];                                    // pitch -> instance, -1 = not held
  int held[MAX] = {};
  int rr = 0;
  int64_t nextSync = 0;
  uint8_t diffSeen[MAX][100] = {};                          // difference seen at the last check (act on the second)
  bool progDiff[MAX] = {};

  void releaseAll();
  void sync();
};

} // namespace t6v
