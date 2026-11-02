// Z80 / U880 CPU core - C++ port of emulator/js/z80.js (same behaviour, same T-states).
#pragma once
#include <cstdint>

namespace t6v {

class Board;

class Z80 {
public:
  explicit Z80(Board& bus) : bus(bus) { reset(); }
  void reset();
  void nmi() { nmiPending = true; }
  int step();                          // one instruction or interrupt, returns T-states

  int a, f, b, c, d, e, h, l;
  int a_, f_, b_, c_, d_, e_, h_, l_;
  int ix, iy, sp, pc, i, r;
  int iff1, iff2, im;
  bool halted, eiDelay, nmiPending;
  int lastPC;

private:
  Board& bus;
  int rd(int a);
  void wr(int a, int v);
  int fetch();
  int fetch16();
  int rd16(int a) { return rd(a) | (rd(a + 1) << 8); }
  void wr16(int a, int v) { wr(a, v); wr(a + 1, v >> 8); }
  void push(int v);
  int pop();
  void incR() { r = (r & 0x80) | ((r + 1) & 0x7F); }
  int disp() { int v = fetch(); return (v & 0x80) ? v - 256 : v; }
  int getBC() const { return (b << 8) | c; }
  int getDE() const { return (d << 8) | e; }
  int getHL() const { return (h << 8) | l; }
  void setBC(int v) { b = (v >> 8) & 0xFF; c = v & 0xFF; }
  void setDE(int v) { d = (v >> 8) & 0xFF; e = v & 0xFF; }
  void setHL(int v) { h = (v >> 8) & 0xFF; l = v & 0xFF; }
  int getRP(int p) const;
  void setRP(int p, int v);
  int getR(int r);
  void setR(int r, int v);
  void add8(int v, int carry);
  void sub8(int v, int carry, bool store);
  void alu(int y, int v);
  int inc8(int v);
  int dec8(int v);
  int add16(int x, int y);
  void adc16(int y);
  void sbc16(int y);
  void daa();
  int rot(int y, int v);
  void bit(int n, int v);
  bool cond(int y) const;
  int acceptInt();
  int exec(int op);
  int execCB();
  int execED();
  int execXY(bool isIX);
};

} // namespace t6v
