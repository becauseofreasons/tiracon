// Tests of the multi-instance engine (12/24/48 voices), no SDK, no DAW.
//   tiracon_multitest
#include "multiengine.h"
#include "uimodel.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace t6v;

static int failures = 0;
static void check(bool ok, const std::string& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) failures++; }

int main() {
  MultiEngine m;
  m.prepare(48000, 512);
  std::vector<float> buf(512);
  auto run = [&](double s) { int blocks = int(s * 48000 / 480); for (int i = 0; i < blocks; i++) m.render(buf.data(), 480); };
  auto press = [&](int code) { m.key(code, true); run(0.14); m.key(code, false); run(0.17); };
  auto activeKeys = [&](int inst) { std::set<int> k; for (int v = 0; v < 6; v++) { int x = m.at(inst).hw().voiceKey(v); if (x >= 0 && m.at(inst).hw().ram[0x58 + v] != 0) k.insert(x); } return k; };
  auto val = [&](int inst, int no) { return ramValue(romInfo(), m.at(inst).ram(), no); };

  std::printf("12 Stimmen (2 Instanzen):\n");
  m.setCount(2);
  run(2.5);
  for (int p = 48; p < 60; p++) { m.noteOn(p, 0.8f); run(0.01); }
  run(0.4);
  std::set<int> k0 = activeKeys(0), k1 = activeKeys(1), all;
  for (int x : k0) all.insert(x);
  for (int x : k1) all.insert(x);
  std::printf("  Instanz 1 spielt %zu Noten, Instanz 2 spielt %zu Noten\n", k0.size(), k1.size());
  check(k0.size() == 6 && k1.size() == 6 && all.size() == 12, "12 Noten gleichzeitig, je 6 pro Instanz");
  bool pitchOk = true;
  for (int i = 0; i < 2; i++) for (int v = 0; v < 6; v++) {
    int key = m.at(i).hw().voiceKey(v);
    double hz = m.at(i).hw().vcoHz(v), want = 440 * std::pow(2.0, (key + 36 - 69) / 12.0);
    if (key >= 0 && std::fabs(1200 * std::log2(hz / want)) > 80) pitchOk = false;      // no AUTO TUNE: component spread
  }
  check(pitchOk, "jede Stimme spielt ihre Note (innerhalb der Bauteilstreuung)");
  for (int p = 48; p < 60; p++) m.noteOff(p);
  run(1.5);
  check(activeKeys(0).empty() && activeKeys(1).empty(), "alle Noten wieder aus");

  m.hostParam(71, 10);
  run(0.3);
  check(val(0, 71) == 10 && val(1, 71) == 10, "Parameter (CUTOFF = 10) in beiden Instanzen");

  press(DIGIT_KEYS[2]); press(DIGIT_KEYS[2]);
  run(0.5);
  int p0 = bcd(m.at(0).ram()[RAM_PROGNO]), p1 = bcd(m.at(1).ram()[RAM_PROGNO]);
  std::printf("  Programm nach Tasten 2, 2: Instanz 1 = %d, Instanz 2 = %d\n", p0, p1);
  check(p0 == 22 && p1 == 22, "Tasten am Bedienfeld wirken in beiden Instanzen");

  m.at(1).hostParam(71, 40);                      // only instance 2: simulated lost bytes
  run(0.3);
  int before = val(1, 71);
  run(0.8);
  std::printf("  CUTOFF nur in Instanz 2 verstellt: %d, nach dem Abgleich %d (Instanz 1: %d)\n", before, val(1, 71), val(0, 71));
  check(before == 40 && val(1, 71) == val(0, 71), "abweichende Instanz wird angeglichen");

  m.at(0).hw().ram[0x188 + 5] ^= 0x55;            // simulated WRITE in instance 1
  run(0.4);
  check(m.at(1).ram()[0x188 + 5] == m.at(0).ram()[0x188 + 5], "gespeicherte Programme werden übernommen");

  std::printf("Unison x2 (P37 = 1):\n");
  m.hostParam(37, 1);
  run(0.5);
  for (int p = 60; p < 66; p++) { m.noteOn(p, 0.8f); run(0.01); }
  run(0.3);
  int busy0 = 0, busy1 = 0;
  for (int v = 0; v < 6; v++) { busy0 += m.at(0).hw().ram[0x58 + v] != 0; busy1 += m.at(1).hw().ram[0x58 + v] != 0; }
  std::printf("  6 Noten: Instanz 1 %d Stimmen, Instanz 2 %d Stimmen belegt\n", busy0, busy1);
  check(busy0 == 6 && busy1 == 6, "6 Noten x2 = 12 Stimmen, je 3 Noten pro Instanz");
  for (int p = 60; p < 66; p++) m.noteOff(p);
  m.hostParam(37, 0);
  run(1.5);

  std::printf("48 Stimmen (8 Instanzen):\n");
  m.setCount(8);
  run(2.5);
  for (int p = 36; p < 84; p++) { m.noteOn(p, 0.7f); run(0.005); }
  float peak = 0;
  for (int i = 0; i < 40; i++) { m.render(buf.data(), 480); for (int k = 0; k < 480; k++) peak = std::max(peak, std::fabs(buf[size_t(k)])); }
  int voices = 0;
  for (int i = 0; i < 8; i++) voices += int(activeKeys(i).size());
  std::printf("  %d Stimmen klingen\n", voices);
  check(voices == 48, "48 Noten gleichzeitig");
  auto t0 = std::chrono::steady_clock::now();
  run(10.0);
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  Rechenzeit: 10 s Audio mit 8 Instanzen in %.2f s = %.0f %% eines Kerns\n", sec, sec * 10);
  std::printf("  Spitzenpegel %.3f\n", peak);
  check(peak > 0.05f && peak <= 1.0f, "Ausgang mit 48 Stimmen hörbar und begrenzt");

  std::printf("Projekt laden (Programm und Editierpuffer):\n");
  m.setCount(2);
  run(2.5);
  m.hostProgram(29);                                    // program 46
  run(0.5);
  int stored13 = val(0, 13);
  m.hostParam(13, stored13 == 0 ? 2 : 0);               // changed, not written
  run(0.5);
  int want13 = val(0, 13);
  std::vector<uint8_t> saved(m.at(0).ram(), m.at(0).ram() + 2048);
  m.loadRam(saved.data());
  run(0.6);
  check(m.at(0).ram()[RAM_PROGNO] == 0x11, "nach dem Einschalten bootet die Firmware in Programm 11");
  run(1.5);
  check(m.at(0).ram()[RAM_PROGNO] == 0x46 && m.at(1).ram()[RAM_PROGNO] == 0x46, "danach wieder Programm 46 (beide Instanzen)");
  check(val(0, 13) == want13 && val(1, 13) == want13 && want13 != stored13, "ungespeicherte Änderung (P13) wieder da");
  m.powerOn();
  run(2.0);
  check(m.at(0).ram()[RAM_PROGNO] == 0x11, "einfacher Reset bleibt in Programm 11");

  std::printf(failures ? "FEHLER: %d Prüfungen\n" : "alle Prüfungen ok\n", failures);
  return failures ? 1 : 0;
}
