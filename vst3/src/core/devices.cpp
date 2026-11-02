// Z80 peripherals - C++ port of emulator/js/devices.js. Keep both in step.
#include "devices.h"
#include "board.h"
#include <algorithm>

namespace t6v {

// ------------------------------------------------------------------ CTC
static uint8_t ctcVector(void* ctx, int n) { return uint8_t((static_cast<CTC*>(ctx)->vector & 0xF8) | (n << 1)); }

CTC::CTC() {
  for (int n = 0; n < 4; n++) { ch[n].n = n; ch[n].src.vectorFn = ctcVector; ch[n].src.ctx = this; ch[n].src.n = n; }
}
void CTC::reset() {
  for (int n = 0; n < 4; n++) {
    Chan& c = ch[n];
    c.ctrl = 0; c.running = false; c.waitTC = false; c.intEnable = false;
    c.src.pending = false; c.src.ius = false;
  }
}
void CTC::write(int n, int v) {
  Chan& c = ch[n];
  if (c.waitTC) {
    c.tc = v == 0 ? 256 : v;
    c.count = c.tc;
    c.pre = 0;
    c.waitTC = false;
    c.running = true;
    return;
  }
  if (v & 1) {
    c.ctrl = v;
    c.intEnable = (v & 0x80) != 0;
    c.counterMode = (v & 0x40) != 0;
    c.prescaler = (v & 0x20) ? 256 : 16;
    if (!c.intEnable) c.src.pending = false;
    if (v & 0x02) c.running = false;        // software reset
    if (v & 0x04) c.waitTC = true;          // time constant follows
  } else if (n == 0) {
    vector = v & 0xF8;
  }
}
// A zero count while the same channel is still under service is lost (see devices.js / Analyse.md 4.1).
void CTC::zero(Chan& c) {
  c.zeroCount++;
  if (!c.intEnable) return;
  if (c.src.ius && dropWhileInService) { c.lost++; return; }
  c.src.pending = true;
}

// ------------------------------------------------------------------ PIO
void PIO::reset() {
  port[0].reset(); port[1].reset();
  notify(0); notify(1);
}
void PIO::notify(int i) { board->pioOutput(id, i, port[i].out, port[i].driveMask()); }
// reg: 0 = A data, 1 = B data, 2 = A control, 3 = B control
void PIO::write(int reg, int v) {
  int i = reg & 1;
  Port& p = port[i];
  if (!(reg & 2)) { p.out = v; notify(i); return; }
  if (p.expectMask) { p.mask = v; p.expectMask = false; notify(i); return; }
  if (p.expectIntMask) { p.intMask = v; p.expectIntMask = false; return; }
  if ((v & 0x0F) == 0x0F) {
    p.mode = v >> 6;
    if (p.mode == 3) p.expectMask = true;
    notify(i);
  } else if ((v & 0x01) == 0) {
    p.vector = v;
  } else if ((v & 0x0F) == 0x07) {
    p.intCtrl = v; p.intEn = (v & 0x80) != 0;
    if (v & 0x10) p.expectIntMask = true;
  } else if ((v & 0x0F) == 0x03) {
    p.intEn = (v & 0x80) != 0;
  }
}
int PIO::read(int reg) {
  int i = reg & 1;
  Port& p = port[i];
  if (reg & 2) return 0xFF;
  if (p.mode == 0) return p.out;
  int pins = board->pioInput(id, i) & 0xFF;
  if (p.mode == 3) return (pins & p.mask) | (p.out & ~p.mask & 0xFF);
  return pins;
}

// ------------------------------------------------------------------ DART (channel B used)
static uint8_t dartVector(void* ctx, int) { return static_cast<DART*>(ctx)->vectorB(); }

DART::DART(const int64_t* clk) : clock(clk) {
  ch[0] = mkChan(0); ch[1] = mkChan(1);
  srcB.vectorFn = dartVector; srcB.ctx = this; srcB.level = true;
}
void DART::reset() {
  ch[0] = mkChan(0); ch[1] = mkChan(1);
  srcB.pending = false; srcB.ius = false;
}
uint8_t DART::vectorB() const {
  const Chan& c = ch[1];
  int v = c.wr[2];
  if (c.wr[1] & 0x04) v = (v & 0xF1) | (2 << 1);     // status affects vector: Rx char available (ch B)
  return uint8_t(v);
}
void DART::updateInt() {
  const Chan& c = ch[1];
  int rxMode = (c.wr[1] >> 3) & 3;
  srcB.pending = rxMode >= 2 && c.fifoLen > 0;
}
void DART::write(int reg, int v) {
  Chan& c = ch[reg & 1];
  if (!(reg & 2)) { txWrite(c, v); return; }
  if (c.ptr == 0) {
    c.ptr = v & 7;
    int cmd = (v >> 3) & 7;
    if (cmd == 3) {                          // channel reset (the JS version writes WR0 into the old object)
      int keepPtr = c.ptr, n = c.n;
      ch[n] = mkChan(n);
      ch[n].ptr = keepPtr;
    } else {
      if (cmd == 6) c.overrun = false;
      c.wr[0] = uint8_t(v);
    }
  } else {
    c.wr[c.ptr] = uint8_t(v);
    c.ptr = 0;
  }
  updateInt();
}
int DART::read(int reg) {
  Chan& c = ch[reg & 1];
  int v;
  if (!(reg & 2)) {
    if (c.fifoLen) { v = c.fifo[0]; c.fifo[0] = c.fifo[1]; c.fifo[1] = c.fifo[2]; c.fifoLen--; }
    else v = 0xFF;
    updateInt();
    return v;
  }
  switch (c.ptr) {
    case 0:
      v = (c.fifoLen ? 0x01 : 0) | (c.txBuf < 0 ? 0x04 : 0) | 0x08 | 0x20;
      if (c.n == 0 && (srcB.pending || srcB.ius)) v |= 0x02;
      break;
    case 1: v = (c.txShift < 0 && c.txBuf < 0 ? 0x01 : 0) | (c.overrun ? 0x20 : 0); break;
    case 2: v = c.n == 1 ? vectorB() : 0xFF; break;
    default: v = 0xFF;
  }
  c.ptr = 0;
  return v;
}
void DART::txWrite(Chan& c, int v) {
  if (c.txShift < 0) { c.txShift = v; c.txDoneAt = *clock + BYTE_CYCLES; }
  else c.txBuf = v;
}
// queue MIDI IN bytes, spaced like a real 31250 Bd line
void DART::receive(const uint8_t* bytes, int n) {
  int64_t t = std::max(*clock, rxLastAt);
  for (int i = 0; i < n; i++) {
    t += BYTE_CYCLES;
    if (rxLen >= RXQ) { rxDropped++; continue; }
    rxQueue[(rxHead + rxLen) % RXQ] = { bytes[i], t };
    rxLen++;
  }
  rxLastAt = t;
}
void DART::tick() {
  int64_t now = *clock;
  if (rxLen && rxQueue[rxHead].at <= now) {
    Chan& c = ch[1];
    while (rxLen && rxQueue[rxHead].at <= now) {
      uint8_t b = rxQueue[rxHead].b;
      rxHead = (rxHead + 1) % RXQ; rxLen--;
      if (!(c.wr[3] & 0x01)) continue;          // receiver disabled
      if (c.fifoLen >= 3) { c.fifo[2] = b; c.overrun = true; overruns++; }
      else c.fifo[c.fifoLen++] = b;
    }
    updateInt();
  }
  for (int i = 0; i < 2; i++) {
    Chan& c = ch[i];
    if (c.txShift >= 0 && now >= c.txDoneAt) {
      if (i == 1 && onTx) onTx(onTxCtx, uint8_t(c.txShift));
      c.txShift = -1;
      if (c.txBuf >= 0) { c.txShift = c.txBuf; c.txBuf = -1; c.txDoneAt = now + BYTE_CYCLES; }
    }
  }
}

} // namespace t6v
