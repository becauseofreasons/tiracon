// Z80 / U880 CPU core - C++ port of emulator/js/z80.js. Keep both in step.
#include "z80.h"
#include "board.h"

namespace t6v {

namespace {
constexpr int FC = 0x01, FN = 0x02, FPV = 0x04, FX = 0x08, FH = 0x10, FY = 0x20, FZ = 0x40, FS = 0x80;

struct Tables {
  uint8_t SZ[256], SZP[256];
  Tables() {
    for (int i = 0; i < 256; i++) {
      int p = 0;
      for (int k = 0; k < 8; k++) p ^= (i >> k) & 1;
      SZ[i] = uint8_t((i & (FS | FX | FY)) | (i == 0 ? FZ : 0));
      SZP[i] = uint8_t(SZ[i] | (p ? 0 : FPV));
    }
  }
};
const Tables T;
#define SZ T.SZ
#define SZP T.SZP

const uint8_t CYC[256] = {
  4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
  8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
  7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
  7, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
  5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 0, 10, 17, 7, 11,
  5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 0, 7, 11,
  5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 0, 7, 11,
  5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 0, 7, 11
};
} // namespace

void Z80::reset() {
  a = 0xFF; f = 0xFF;
  b = c = d = e = h = l = 0;
  a_ = f_ = b_ = c_ = d_ = e_ = h_ = l_ = 0;
  ix = 0xFFFF; iy = 0xFFFF;
  sp = 0xFFFF; pc = 0;
  i = 0; r = 0;
  iff1 = 0; iff2 = 0; im = 0;
  halted = false; eiDelay = false; nmiPending = false;
  lastPC = 0;
}

int Z80::rd(int a) { return bus.read(a & 0xFFFF); }
void Z80::wr(int a, int v) { bus.write(a & 0xFFFF, v & 0xFF); }
int Z80::fetch() { int v = bus.read(pc); pc = (pc + 1) & 0xFFFF; return v; }
int Z80::fetch16() { int lo = fetch(); return lo | (fetch() << 8); }
void Z80::push(int v) {
  sp = (sp - 1) & 0xFFFF; wr(sp, v >> 8);
  sp = (sp - 1) & 0xFFFF; wr(sp, v);
}
int Z80::pop() {
  int v = rd(sp) | (rd(sp + 1) << 8);
  sp = (sp + 2) & 0xFFFF;
  return v;
}

int Z80::getRP(int p) const {
  switch (p) {
    case 0: return (b << 8) | c;
    case 1: return (d << 8) | e;
    case 2: return (h << 8) | l;
    default: return sp;
  }
}
void Z80::setRP(int p, int v) {
  v &= 0xFFFF;
  switch (p) {
    case 0: b = v >> 8; c = v & 0xFF; break;
    case 1: d = v >> 8; e = v & 0xFF; break;
    case 2: h = v >> 8; l = v & 0xFF; break;
    default: sp = v;
  }
}
int Z80::getR(int rr) {
  switch (rr) {
    case 0: return b; case 1: return c; case 2: return d; case 3: return e;
    case 4: return h; case 5: return l; case 6: return rd((h << 8) | l);
    default: return a;
  }
}
void Z80::setR(int rr, int v) {
  v &= 0xFF;
  switch (rr) {
    case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
    case 4: h = v; break; case 5: l = v; break; case 6: wr((h << 8) | l, v); break;
    default: a = v;
  }
}

// ---------------------------------------------------------------- ALU
void Z80::add8(int v, int carry) {
  int x = a, res = x + v + carry;
  f = SZ[res & 0xFF] | ((res >> 8) & FC) | ((x ^ v ^ res) & FH) | ((((x ^ ~v) & (x ^ res)) & 0x80) ? FPV : 0);
  a = res & 0xFF;
}
void Z80::sub8(int v, int carry, bool store) {
  int x = a, res = x - v - carry;
  int ff = FN | ((res >> 8) & FC) | ((x ^ v ^ res) & FH) | ((((x ^ v) & (x ^ res)) & 0x80) ? FPV : 0);
  if (store) { f = ff | SZ[res & 0xFF]; a = res & 0xFF; }
  else f = ff | (SZ[res & 0xFF] & (FS | FZ)) | (v & (FX | FY));       // CP: X/Y from operand
}
void Z80::alu(int y, int v) {
  switch (y) {
    case 0: add8(v, 0); break;
    case 1: add8(v, f & FC); break;
    case 2: sub8(v, 0, true); break;
    case 3: sub8(v, f & FC, true); break;
    case 4: a &= v; f = SZP[a] | FH; break;
    case 5: a ^= v; f = SZP[a]; break;
    case 6: a |= v; f = SZP[a]; break;
    default: sub8(v, 0, false);
  }
}
int Z80::inc8(int v) {
  int rr = (v + 1) & 0xFF;
  f = (f & FC) | SZ[rr] | ((rr & 0x0F) == 0 ? FH : 0) | (rr == 0x80 ? FPV : 0);
  return rr;
}
int Z80::dec8(int v) {
  int rr = (v - 1) & 0xFF;
  f = (f & FC) | FN | SZ[rr] | ((rr & 0x0F) == 0x0F ? FH : 0) | (rr == 0x7F ? FPV : 0);
  return rr;
}
int Z80::add16(int x, int y) {
  int res = x + y;
  f = (f & (FS | FZ | FPV)) | ((res >> 16) & FC) | (((x ^ y ^ res) >> 8) & FH) | ((res >> 8) & (FX | FY));
  return res & 0xFFFF;
}
void Z80::adc16(int y) {
  int x = getHL(), res = x + y + (f & FC);
  f = ((res >> 16) & FC) | ((res >> 8) & (FS | FX | FY)) | ((res & 0xFFFF) ? 0 : FZ) |
      (((x ^ y ^ res) >> 8) & FH) | ((((x ^ ~y) & (x ^ res)) & 0x8000) ? FPV : 0);
  setHL(res);
}
void Z80::sbc16(int y) {
  int x = getHL(), res = x - y - (f & FC);
  f = FN | ((res >> 16) & FC) | ((res >> 8) & (FS | FX | FY)) | ((res & 0xFFFF) ? 0 : FZ) |
      (((x ^ y ^ res) >> 8) & FH) | ((((x ^ y) & (x ^ res)) & 0x8000) ? FPV : 0);
  setHL(res);
}
void Z80::daa() {
  int x = a, ff = f, corr = 0, carry = ff & FC, hh;
  if ((ff & FH) || (x & 0x0F) > 9) corr |= 0x06;
  if (carry || x > 0x99) { corr |= 0x60; carry = FC; }
  if (ff & FN) { hh = ((ff & FH) && (x & 0x0F) < 6) ? FH : 0; x = (x - corr) & 0xFF; }
  else { hh = ((x & 0x0F) > 9) ? FH : 0; x = (x + corr) & 0xFF; }
  a = x;
  f = (ff & FN) | hh | carry | SZP[x];
}
int Z80::rot(int y, int v) {
  int cc, rr;
  switch (y) {
    case 0: cc = v >> 7; rr = ((v << 1) | cc) & 0xFF; break;          // RLC
    case 1: cc = v & 1; rr = (v >> 1) | (cc << 7); break;             // RRC
    case 2: cc = v >> 7; rr = ((v << 1) | (f & FC)) & 0xFF; break;     // RL
    case 3: cc = v & 1; rr = (v >> 1) | ((f & FC) << 7); break;        // RR
    case 4: cc = v >> 7; rr = (v << 1) & 0xFF; break;                 // SLA
    case 5: cc = v & 1; rr = (v >> 1) | (v & 0x80); break;            // SRA
    case 6: cc = v >> 7; rr = ((v << 1) | 1) & 0xFF; break;           // SLL (undoc.)
    default: cc = v & 1; rr = v >> 1;                                 // SRL
  }
  f = SZP[rr] | cc;
  return rr;
}
void Z80::bit(int n, int v) {
  int m = v & (1 << n);
  f = (f & FC) | FH | (v & (FX | FY)) | (m ? (n == 7 ? FS : 0) : (FZ | FPV));
}
bool Z80::cond(int y) const {
  switch (y) {
    case 0: return !(f & FZ); case 1: return (f & FZ) != 0;
    case 2: return !(f & FC); case 3: return (f & FC) != 0;
    case 4: return !(f & FPV); case 5: return (f & FPV) != 0;
    case 6: return !(f & FS); default: return (f & FS) != 0;
  }
}

// ---------------------------------------------------------------- step
int Z80::step() {
  if (nmiPending) {
    nmiPending = false;
    if (halted) { halted = false; pc = (pc + 1) & 0xFFFF; }
    incR();
    iff2 = iff1; iff1 = 0;
    push(pc);
    pc = 0x0066;
    return 11;
  }
  if (iff1 && !eiDelay && bus.intRequested()) return acceptInt();
  eiDelay = false;
  lastPC = pc;
  if (halted) { incR(); return 4; }
  incR();
  return exec(fetch());
}

int Z80::acceptInt() {
  int vec = bus.intAck() & 0xFF;
  if (halted) { halted = false; pc = (pc + 1) & 0xFFFF; }
  incR();
  iff1 = 0; iff2 = 0;
  push(pc);
  if (im == 2) { pc = rd16((i << 8) | (vec & 0xFE)); return 19; }
  if (im == 1) { pc = 0x38; return 13; }
  pc = vec & 0x38;     // IM 0: treat vector as RST
  return 13;
}

int Z80::exec(int op) {
  int t = CYC[op], x = op >> 6, y = (op >> 3) & 7, z = op & 7, v, w;
  (void)w;
  if (x == 1) {
    if (op == 0x76) { halted = true; pc = (pc - 1) & 0xFFFF; return 4; }
    setR(y, getR(z));
    return t;
  }
  if (x == 2) { alu(y, getR(z)); return t; }
  switch (op) {
    case 0x00: return t;
    case 0x01: case 0x11: case 0x21: case 0x31: setRP(op >> 4, fetch16()); return t;
    case 0x02: wr(getBC(), a); return t;
    case 0x12: wr(getDE(), a); return t;
    case 0x0A: a = rd(getBC()); return t;
    case 0x1A: a = rd(getDE()); return t;
    case 0x22: wr16(fetch16(), getHL()); return t;
    case 0x2A: setHL(rd16(fetch16())); return t;
    case 0x32: wr(fetch16(), a); return t;
    case 0x3A: a = rd(fetch16()); return t;
    case 0x03: case 0x13: case 0x23: case 0x33: setRP(op >> 4, getRP(op >> 4) + 1); return t;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: setRP(op >> 4, getRP(op >> 4) - 1); return t;
    case 0x09: case 0x19: case 0x29: case 0x39: setHL(add16(getHL(), getRP(op >> 4))); return t;
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C:
      setR(y, inc8(getR(y))); return t;
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D:
      setR(y, dec8(getR(y))); return t;
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
      setR(y, fetch()); return t;
    case 0x07: v = a; a = ((v << 1) | (v >> 7)) & 0xFF; f = (f & (FS | FZ | FPV)) | (a & (FX | FY)) | (v >> 7); return t;
    case 0x0F: v = a; a = ((v >> 1) | (v << 7)) & 0xFF; f = (f & (FS | FZ | FPV)) | (a & (FX | FY)) | (v & 1); return t;
    case 0x17: v = a; a = ((v << 1) | (f & FC)) & 0xFF; f = (f & (FS | FZ | FPV)) | (a & (FX | FY)) | (v >> 7); return t;
    case 0x1F: v = a; a = (v >> 1) | ((f & FC) << 7); f = (f & (FS | FZ | FPV)) | (a & (FX | FY)) | (v & 1); return t;
    case 0x27: daa(); return t;
    case 0x2F: a ^= 0xFF; f = (f & (FS | FZ | FPV | FC)) | FH | FN | (a & (FX | FY)); return t;
    case 0x37: f = (f & (FS | FZ | FPV)) | FC | (a & (FX | FY)); return t;
    case 0x3F: f = ((f & (FS | FZ | FPV | FC)) | ((f & FC) << 4) | (a & (FX | FY))) ^ FC; return t;
    case 0x08: v = a; a = a_; a_ = v; v = f; f = f_; f_ = v; return t;
    case 0x10:
      v = disp(); b = (b - 1) & 0xFF;
      if (b) { pc = (pc + v) & 0xFFFF; return 13; }
      return 8;
    case 0x18: v = disp(); pc = (pc + v) & 0xFFFF; return t;
    case 0x20: case 0x28: case 0x30: case 0x38:
      v = disp();
      if (cond(y - 4)) { pc = (pc + v) & 0xFFFF; return 12; }
      return 7;
    // ---- x == 3
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
      if (cond(y)) { pc = pop(); return 11; }
      return 5;
    case 0xC1: setBC(pop()); return t;
    case 0xD1: setDE(pop()); return t;
    case 0xE1: setHL(pop()); return t;
    case 0xF1: v = pop(); a = v >> 8; f = v & 0xFF; return t;
    case 0xC5: push(getBC()); return t;
    case 0xD5: push(getDE()); return t;
    case 0xE5: push(getHL()); return t;
    case 0xF5: push((a << 8) | f); return t;
    case 0xC9: pc = pop(); return t;
    case 0xD9:
      v = b; b = b_; b_ = v; v = c; c = c_; c_ = v;
      v = d; d = d_; d_ = v; v = e; e = e_; e_ = v;
      v = h; h = h_; h_ = v; v = l; l = l_; l_ = v;
      return t;
    case 0xE9: pc = getHL(); return t;
    case 0xF9: sp = getHL(); return t;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA:
      v = fetch16(); if (cond(y)) pc = v; return t;
    case 0xC3: pc = fetch16(); return t;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:
      v = fetch16();
      if (cond(y)) { push(pc); pc = v; return 17; }
      return 10;
    case 0xCD: v = fetch16(); push(pc); pc = v; return t;
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
      alu(y, fetch()); return t;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
      push(pc); pc = y * 8; return t;
    case 0xD3: v = fetch(); bus.ioWrite((a << 8) | v, a); return t;
    case 0xDB: v = fetch(); a = bus.ioRead((a << 8) | v); return t;
    case 0xE3: v = rd16(sp); wr16(sp, getHL()); setHL(v); return t;
    case 0xEB: v = d; d = h; h = v; v = e; e = l; l = v; return t;
    case 0xF3: iff1 = 0; iff2 = 0; return t;
    case 0xFB: iff1 = 1; iff2 = 1; eiDelay = true; return t;
    case 0xCB: return execCB();
    case 0xED: return execED();
    case 0xDD: return execXY(true);
    case 0xFD: return execXY(false);
  }
  return t;
}

int Z80::execCB() {
  incR();
  int op = fetch(), x = op >> 6, y = (op >> 3) & 7, z = op & 7, v = getR(z);
  if (x == 0) { setR(z, rot(y, v)); return z == 6 ? 15 : 8; }
  if (x == 1) { bit(y, v); return z == 6 ? 12 : 8; }
  if (x == 2) { setR(z, v & ~(1 << y)); return z == 6 ? 15 : 8; }
  setR(z, v | (1 << y)); return z == 6 ? 15 : 8;
}

int Z80::execED() {
  incR();
  int op = fetch(), x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1, v, hl;
  if (x == 1) {
    switch (z) {
      case 0:
        v = bus.ioRead(getBC());
        if (y != 6) setR(y, v);
        f = (f & FC) | SZP[v];
        return 12;
      case 1: bus.ioWrite(getBC(), y == 6 ? 0 : getR(y)); return 12;
      case 2: if (q == 0) sbc16(getRP(p)); else adc16(getRP(p)); return 15;
      case 3:
        v = fetch16();
        if (q == 0) wr16(v, getRP(p)); else setRP(p, rd16(v));
        return 20;
      case 4: v = a; a = 0; sub8(v, 0, true); return 8;
      case 5: pc = pop(); iff1 = iff2; if (y == 1) bus.reti(); return 14;
      case 6: { static const int IM[8] = { 0, 0, 1, 2, 0, 0, 1, 2 }; im = IM[y]; return 8; }
      default:
        switch (y) {
          case 0: i = a; return 9;
          case 1: r = a; return 9;
          case 2: a = i; f = (f & FC) | SZ[a] | (iff2 ? FPV : 0); return 9;
          case 3: a = r; f = (f & FC) | SZ[a] | (iff2 ? FPV : 0); return 9;
          case 4:   // RRD
            hl = getHL(); v = rd(hl);
            wr(hl, ((a << 4) | (v >> 4)) & 0xFF);
            a = (a & 0xF0) | (v & 0x0F);
            f = (f & FC) | SZP[a];
            return 18;
          case 5:   // RLD
            hl = getHL(); v = rd(hl);
            wr(hl, ((v << 4) | (a & 0x0F)) & 0xFF);
            a = (a & 0xF0) | (v >> 4);
            f = (f & FC) | SZP[a];
            return 18;
          default: return 8;
        }
    }
  }
  if (x == 2 && z <= 3 && y >= 4) {
    int inc = (y & 1) ? -1 : 1, bc, n;
    bool rep = y >= 6;
    switch (z) {
      case 0: // LDI LDD LDIR LDDR
        hl = getHL(); v = rd(hl);
        wr(getDE(), v);
        setHL(hl + inc); setDE(getDE() + inc);
        bc = (getBC() - 1) & 0xFFFF; setBC(bc);
        n = (v + a) & 0xFF;
        f = (f & (FS | FZ | FC)) | (bc ? FPV : 0) | (n & FX) | ((n & 0x02) << 4);
        if (rep && bc) { pc = (pc - 2) & 0xFFFF; return 21; }
        return 16;
      case 1: { // CPI CPD CPIR CPDR
        hl = getHL(); v = rd(hl);
        int aa = (a - v) & 0xFF;
        setHL(hl + inc);
        bc = (getBC() - 1) & 0xFFFF; setBC(bc);
        int hf = (a ^ v ^ aa) & FH;
        n = (aa - (hf ? 1 : 0)) & 0xFF;
        f = (f & FC) | FN | (SZ[aa] & (FS | FZ)) | hf | (bc ? FPV : 0) | (n & FX) | ((n & 0x02) << 4);
        if (rep && bc && aa != 0) { pc = (pc - 2) & 0xFFFF; return 21; }
        return 16;
      }
      case 2: // INI IND INIR INDR
        v = bus.ioRead(getBC());
        hl = getHL(); wr(hl, v); setHL(hl + inc);
        b = (b - 1) & 0xFF;
        f = (b ? 0 : FZ) | (SZ[b] & (FS | FX | FY)) | ((v & 0x80) ? FN : 0);
        if (rep && b) { pc = (pc - 2) & 0xFFFF; return 21; }
        return 16;
      default: // OUTI OUTD OTIR OTDR
        hl = getHL(); v = rd(hl);
        b = (b - 1) & 0xFF;
        bus.ioWrite(getBC(), v);
        setHL(hl + inc);
        n = v + l;
        f = (SZ[b] & (FS | FZ | FX | FY)) | ((v & 0x80) ? FN : 0) | (n > 255 ? (FH | FC) : 0) | (SZP[(n & 7) ^ b] & FPV);
        if (rep && b) { pc = (pc - 2) & 0xFFFF; return 21; }
        return 16;
    }
  }
  return 8;   // undefined ED opcode = NOP NOP
}

int Z80::execXY(bool isIX) {
  incR();
  int xy = isIX ? ix : iy;
  int op = fetch(), x = op >> 6, y = (op >> 3) & 7, z = op & 7, v, dd, addr;
  auto setXY = [&](int val) { if (isIX) ix = val & 0xFFFF; else iy = val & 0xFFFF; };
  auto getRX = [&](int rr) -> int {          // H/L -> XYH/XYL
    if (rr == 4) return (isIX ? ix : iy) >> 8;
    if (rr == 5) return (isIX ? ix : iy) & 0xFF;
    return getR(rr);
  };
  auto setRX = [&](int rr, int val) {
    val &= 0xFF;
    int cur = isIX ? ix : iy;
    if (rr == 4) setXY((cur & 0x00FF) | (val << 8));
    else if (rr == 5) setXY((cur & 0xFF00) | val);
    else setR(rr, val);
  };
  if (x == 1 && op != 0x76) {
    if (y == 6) { addr = (xy + disp()) & 0xFFFF; wr(addr, getR(z)); return 19; }
    if (z == 6) { addr = (xy + disp()) & 0xFFFF; setR(y, rd(addr)); return 19; }
    if (y == 4 || y == 5 || z == 4 || z == 5) { setRX(y, getRX(z)); return 8; }
    return 4 + exec(op);
  }
  if (x == 2) {
    if (z == 6) { addr = (xy + disp()) & 0xFFFF; alu(y, rd(addr)); return 19; }
    if (z == 4 || z == 5) { alu(y, getRX(z)); return 8; }
    return 4 + exec(op);
  }
  switch (op) {
    case 0x09: case 0x19: case 0x29: case 0x39:
      v = (op == 0x29) ? xy : getRP(op >> 4);
      setXY(add16(xy, v)); return 15;
    case 0x21: setXY(fetch16()); return 14;
    case 0x22: wr16(fetch16(), xy); return 20;
    case 0x2A: setXY(rd16(fetch16())); return 20;
    case 0x23: setXY(xy + 1); return 10;
    case 0x2B: setXY(xy - 1); return 10;
    case 0x24: setRX(4, inc8(getRX(4))); return 8;
    case 0x25: setRX(4, dec8(getRX(4))); return 8;
    case 0x2C: setRX(5, inc8(getRX(5))); return 8;
    case 0x2D: setRX(5, dec8(getRX(5))); return 8;
    case 0x26: setRX(4, fetch()); return 11;
    case 0x2E: setRX(5, fetch()); return 11;
    case 0x34: addr = (xy + disp()) & 0xFFFF; wr(addr, inc8(rd(addr))); return 23;
    case 0x35: addr = (xy + disp()) & 0xFFFF; wr(addr, dec8(rd(addr))); return 23;
    case 0x36: addr = (xy + disp()) & 0xFFFF; wr(addr, fetch()); return 19;
    case 0xE1: setXY(pop()); return 14;
    case 0xE5: push(xy); return 15;
    case 0xE3: v = rd16(sp); wr16(sp, xy); setXY(v); return 23;
    case 0xE9: pc = xy; return 8;
    case 0xF9: sp = xy; return 10;
    case 0xCB: {
      dd = disp(); addr = (xy + dd) & 0xFFFF;
      int op2 = fetch(), x2 = op2 >> 6, y2 = (op2 >> 3) & 7, z2 = op2 & 7, rr;
      v = rd(addr);
      if (x2 == 1) { bit(y2, v); return 20; }
      if (x2 == 0) rr = rot(y2, v);
      else if (x2 == 2) rr = v & ~(1 << y2);
      else rr = v | (1 << y2);
      wr(addr, rr);
      if (z2 != 6) setR(z2, rr);   // undocumented register copy
      return 23;
    }
    case 0xDD: case 0xFD: case 0xED:
      pc = (pc - 1) & 0xFFFF;      // prefix acts as NOP
      return 4;
  }
  return 4 + exec(op);             // prefix has no effect
}

} // namespace t6v
