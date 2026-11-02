// Z80 peripherals of the DLP: CTC, PIO, DART - C++ port of emulator/js/devices.js.
// Time base: clock = CPU T-states since start (4 MHz). No allocation after construction.
#pragma once
#include <cstdint>

namespace t6v {

// one entry in the interrupt daisy chain
struct IntSource {
  bool pending = false, ius = false, level = false;
  uint32_t count = 0;
  uint8_t (*vectorFn)(void*, int) = nullptr;
  void* ctx = nullptr;
  int n = 0;
  uint8_t ack() {
    ius = true;
    if (!level) pending = false;
    count++;
    return vectorFn(ctx, n);
  }
};

struct CTC {
  struct Chan {
    int n = 0, ctrl = 0, tc = 256, count = 256, pre = 0, prescaler = 256;
    bool running = false, waitTC = false, counterMode = false, intEnable = false;
    uint32_t zeroCount = 0, lost = 0;
    IntSource src;
  };
  int vector = 0;
  bool dropWhileInService = true;
  Chan ch[4];
  CTC();
  void reset();
  void write(int n, int v);
  int read(int n) const { return ch[n].count & 0xFF; }
  void zero(Chan& c);
  void tick(int t) {
    for (int n = 0; n < 4; n++) {
      Chan& c = ch[n];
      if (!c.running || c.counterMode) continue;
      c.pre += t;
      while (c.pre >= c.prescaler) {
        c.pre -= c.prescaler;
        if (--c.count <= 0) { c.count = c.tc; zero(c); }
      }
    }
  }
  void trigger(int n) {                  // active edge on CLK/TRGn
    Chan& c = ch[n];
    if (c.running && c.counterMode) {
      if (--c.count <= 0) { c.count = c.tc; zero(c); }
    }
  }
};

class Board;

struct PIO {
  struct Port {
    int mode = 1, mask = 0xFF, out = 0, vector = 0, intCtrl = 0, intMask = 0xFF;
    bool expectMask = false, expectIntMask = false, intEn = false;
    void reset() { *this = Port(); }
    int driveMask() const {              // bits actively driven by the PIO
      if (mode == 0) return 0xFF;
      if (mode == 3) return (~mask) & 0xFF;
      return 0;
    }
  };
  int id;                                // which PIO on the board (wiring in Board)
  Board* board;
  Port port[2];
  PIO(int id, Board* b) : id(id), board(b) {}
  void reset();
  void notify(int i);
  void write(int reg, int v);
  int read(int reg);
};

struct DART {
  static constexpr int BYTE_CYCLES = 1280;   // 10 bit at 31250 Bd = 320 us = 1280 T-states at 4 MHz
  static constexpr int RXQ = 4096;           // MIDI-in queue (bytes waiting for the serial line)
  struct Chan {
    int n = 0;
    uint8_t wr[8] = {};
    int ptr = 0;
    uint8_t fifo[3] = {};
    int fifoLen = 0;
    int txBuf = -1, txShift = -1;
    int64_t txDoneAt = 0;
    bool overrun = false;
  };
  struct RxItem { uint8_t b; int64_t at; };
  const int64_t* clock;
  Chan ch[2];
  IntSource srcB;
  RxItem rxQueue[RXQ];
  int rxHead = 0, rxLen = 0;
  int64_t rxLastAt = 0;
  uint32_t overruns = 0, rxDropped = 0;
  void (*onTx)(void*, uint8_t) = nullptr;
  void* onTxCtx = nullptr;

  explicit DART(const int64_t* clock);
  static Chan mkChan(int n) { Chan c; c.n = n; return c; }
  void reset();
  uint8_t vectorB() const;
  void updateInt();
  void write(int reg, int v);
  int read(int reg);
  void txWrite(Chan& c, int v);
  void receive(const uint8_t* bytes, int n);
  void clearRx() { rxHead = 0; rxLen = 0; }
  void tick();
};

} // namespace t6v
