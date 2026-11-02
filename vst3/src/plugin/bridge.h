// Processor <-> editor exchange. Processor and controller live in the same process (the plugin is
// not "distributable"); the processor hands the controller a pointer to this object once (IMessage).
// - snapshot: written by the audio thread ~60x per second (try_lock, never blocks), read by the editor
// - commands: editor -> audio thread, single producer / single consumer ring without locks
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace tiracon {

struct UiSnapshot {
  uint32_t seq = 0;
  double time = 0;                 // emulated seconds
  float disp[64] = {};             // display brightness (Board::takeDisplay)
  uint8_t ram[2048] = {};
  int instances = 1;               // running Tiracons
  struct Voice { int key = -1; int state = 0; float hz = 0, ega = 0, egf = 0; } voices[48];   // 6 per instance
  float cv[28] = {};               // levels 0..1 (t6v::Cv order)
  bool cvHas[28] = {};
  uint8_t wave[5] = {};            // SSZ SRE SPW SF2 SF4
  float peak = 0;
  char serial[24] = {};
};

struct UiCommand {
  enum Type : uint8_t { BlpKey, Note, Param, Reset, Instances } type;
  uint8_t a, b;                    // BlpKey: code, down / Note: pitch, on / Param: number, value (CC) / Instances: count
};

class UiBridge {
public:
  void addRef() { refs.fetch_add(1); }
  void release() { if (refs.fetch_sub(1) == 1) delete this; }

  // audio thread
  bool tryPublish(const UiSnapshot& s) {
    if (!mtx.try_lock()) return false;
    snap = s;
    snap.seq = ++seq;
    mtx.unlock();
    return true;
  }
  bool pop(UiCommand& c) {
    uint32_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    c = ring[t % N];
    tail.store(t + 1, std::memory_order_release);
    return true;
  }
  // editor thread
  void read(UiSnapshot& out) { std::lock_guard<std::mutex> l(mtx); out = snap; }
  bool push(const UiCommand& c) {
    uint32_t h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= N) return false;
    ring[h % N] = c;
    head.store(h + 1, std::memory_order_release);
    return true;
  }

private:
  static constexpr uint32_t N = 256;
  std::atomic<int> refs{ 1 };
  std::mutex mtx;
  UiSnapshot snap;
  uint32_t seq = 0;
  UiCommand ring[N] = {};
  std::atomic<uint32_t> head{ 0 }, tail{ 0 };
};

} // namespace tiracon
