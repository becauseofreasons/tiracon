// End-to-end test of the built plugin: loads Tiracon.vst3 like a DAW does, plays notes, moves
// parameters, selects a program, checks pitch, feedback to the host and the saved state.
//   tiracon_hosttest <Tiracon.vst3> [out.wav]
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static const double FS = 48000;
static const int BLOCK = 512;
static int failures = 0;
static void check(bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) failures++; }

// strongest frequency within +-range cents around f0 (Goertzel, 1 cent steps)
static double pitchNear(const std::vector<float>& x, double t0, double t1, double f0, double range = 150) {
  size_t s = size_t(t0 * FS), e = size_t(t1 * FS);
  double best = 0, bestF = 0;
  for (int c = -int(range); c <= int(range); c++) {
    double f = f0 * std::pow(2.0, c / 1200.0), w = 2 * M_PI * f / FS, k = 2 * std::cos(w), s1 = 0, s2 = 0;
    for (size_t i = s; i < e; i++) {
      double win = 0.5 - 0.5 * std::cos(2 * M_PI * double(i - s) / double(e - s));
      double s0 = x[i] * win + k * s1 - s2; s2 = s1; s1 = s0;
    }
    double m = s1 * s1 + s2 * s2 - k * s1 * s2;
    if (m > best) { best = m; bestF = f; }
  }
  return bestF;
}
static double rms(const std::vector<float>& x, double t0, double t1) {
  double q = 0; size_t s = size_t(t0 * FS), e = size_t(t1 * FS);
  for (size_t i = s; i < e; i++) q += double(x[i]) * x[i];
  return std::sqrt(q / double(e - s));
}
static double cents(double f, double ref) { return 1200 * std::log2(f / ref); }

static void writeWav(const char* path, const std::vector<float>& x) {
  std::ofstream f(path, std::ios::binary);
  auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
  auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
  f.write("RIFF", 4); u32(uint32_t(36 + x.size() * 2)); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
  u32(uint32_t(FS)); u32(uint32_t(FS * 2)); u16(2); u16(16); f.write("data", 4); u32(uint32_t(x.size() * 2));
  for (float v : x) { int s = int(std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767)); u16(uint16_t(int16_t(s))); }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "tiracon_hosttest <Tiracon.vst3> [out.wav]\n"); return 2; }
  std::string err;
  auto module = VST3::Hosting::Module::create(argv[1], err);
  if (!module) { std::fprintf(stderr, "Modul nicht geladen: %s\n", err.c_str()); return 1; }
  HostApplication host;
  PluginContextFactory::instance().setPluginContext(&host);
  auto factory = module->getFactory();
  VST3::Hosting::ClassInfo info;
  bool found = false;
  for (auto& ci : factory.classInfos()) if (ci.category() == kVstAudioEffectClass) { info = ci; found = true; break; }
  if (!found) { std::fprintf(stderr, "keine Audio-Klasse\n"); return 1; }
  std::printf("%s (%s, %s) %s\n", info.name().c_str(), info.vendor().c_str(), info.subCategoriesString().c_str(), info.version().c_str());

  auto provider = owned(new PlugProvider(factory, info, true));
  if (!provider->initialize()) { std::fprintf(stderr, "PlugProvider fehlgeschlagen\n"); return 1; }
  IPtr<IComponent> comp = provider->getComponentPtr();
  IPtr<IEditController> ctrl = provider->getControllerPtr();
  FUnknownPtr<IAudioProcessor> proc(comp);
  check(comp && ctrl && proc, "Komponente, Controller und Prozessor geladen");

  // controller gets the processor state like in a DAW
  {
    MemoryStream st;
    comp->getState(&st);
    st.seek(0, IBStream::kIBSeekSet, nullptr);
    ctrl->setComponentState(&st);
  }
  int32 nParams = ctrl->getParameterCount();
  std::printf("  %d Parameter\n", nParams);
  check(nParams == 46 + 3, "46 Firmware-Parameter + Volume, Pitch Bend, Programm");

  ProcessSetup setup{ kRealtime, kSample32, BLOCK, FS };
  check(proc->setupProcessing(setup) == kResultOk, "setupProcessing");
  comp->activateBus(kAudio, kOutput, 0, true);
  comp->activateBus(kEvent, kInput, 0, true);
  comp->setActive(true);
  proc->setProcessing(true);

  HostProcessData data;
  data.prepare(*comp, BLOCK, kSample32);
  EventList events;
  ParameterChanges inCh(64), outCh(64);
  data.inputEvents = &events;
  data.inputParameterChanges = &inCh;
  data.outputParameterChanges = &outCh;
  data.numSamples = BLOCK;

  std::vector<float> out;
  std::map<ParamID, std::vector<std::pair<double, double>>> feedback;    // id -> (time, value)
  struct Ev { double t; int kind; int a; double v; };                 // kind 0 note on, 1 note off, 2 param
  std::vector<Ev> plan = {
    { 2.00, 0, 60, 0.8 }, { 2.50, 2, 201, 1.0 }, { 2.97, 2, 201, 0.5 }, { 3.00, 1, 60, 0 },   // program 11, bend +1
    { 3.20, 2, 71, 10 / 63.0 },                                         // P71 CUTOFF from the host
    { 3.50, 2, 202, 9 / 31.0 },                                         // program 22 (index 9)
    { 4.50, 0, 60, 0.8 }, { 6.20, 1, 60, 0 },
  };
  size_t pi = 0;
  const double end = 7.0;
  for (int64_t pos = 0; pos < int64_t(end * FS); pos += BLOCK) {
    events.clear();
    inCh.clearQueue();
    outCh.clearQueue();
    double t0 = pos / FS, t1 = (pos + BLOCK) / FS;
    while (pi < plan.size() && plan[pi].t < t1) {
      const Ev& ev = plan[pi++];
      int32 off = int32((ev.t - t0) * FS);
      if (ev.kind <= 1) {
        Event e{};
        e.busIndex = 0; e.sampleOffset = off; e.flags = Event::kIsLive;
        if (ev.kind == 0) { e.type = Event::kNoteOnEvent; e.noteOn.channel = 0; e.noteOn.pitch = int16(ev.a); e.noteOn.velocity = float(ev.v); e.noteOn.noteId = -1; }
        else { e.type = Event::kNoteOffEvent; e.noteOff.channel = 0; e.noteOff.pitch = int16(ev.a); e.noteOff.velocity = 0.5f; e.noteOff.noteId = -1; }
        events.addEvent(e);
      } else {
        int32 idx;
        if (IParamValueQueue* q = inCh.addParameterData(ParamID(ev.a), idx)) q->addPoint(off, ev.v, idx);
        ctrl->setParamNormalized(ParamID(ev.a), ev.v);
      }
    }
    if (proc->process(data) != kResultOk) { check(false, "process"); break; }
    float* L = data.outputs[0].channelBuffers32[0];
    float* R = data.outputs[0].channelBuffers32[1];
    for (int i = 0; i < BLOCK; i++) { out.push_back(L[i]); if (L[i] != R[i]) { check(false, "L = R"); return 1; } }
    for (int32 i = 0; i < outCh.getParameterCount(); i++) {
      IParamValueQueue* q = outCh.getParameterData(i);
      int32 o; ParamValue v;
      if (q && q->getPoint(0, o, v) == kResultTrue) { feedback[q->getParameterId()].push_back({ t1, v }); ctrl->setParamNormalized(q->getParameterId(), v); }
    }
  }
  proc->setProcessing(false);

  std::printf("Klang:\n");
  double silent = rms(out, 0.2, 1.8), note = rms(out, 2.3, 2.9);
  std::printf("  Pegel vor der Note %.4f, mit Note %.4f\n", silent, note);
  check(note > 0.02 && note > 20 * silent, "Note hörbar, vorher still");
  double f1 = pitchNear(out, 2.15, 2.48, 261.626);
  std::printf("  Note 60 (Programm 11, OCTAVE LOW): %.2f Hz = %+.1f Cent zu C4 (Gerät 1266 ohne AUTO TUNE)\n", f1, cents(f1, 261.626));
  check(std::fabs(cents(f1, 261.626)) < 60, "Tonhöhe C4 (innerhalb der Bauteilstreuung)");
  double f3 = pitchNear(out, 2.62, 2.95, 277.18);
  std::printf("  mit Pitch Bend +1: %.2f Hz (%+.1f Cent)\n", f3, cents(f3, f1));
  check(std::fabs(cents(f3, f1) - 100) < 5, "Pitch Bend +1 Halbton");
  // program 22 has a strong vibrato (LFO VCO, about +-2 semitones): no pitch measurement, the octave is
  // checked through the feedback of P11 below
  double r22 = rms(out, 4.7, 6.15);
  std::printf("  Programm 22 (OCTAVE HIGH, Vibrato): Pegel %.4f\n", r22);
  check(r22 > 0.02, "Programm 22 klingt");

  std::printf("Rückmeldung an die DAW:\n");
  auto last = [&](ParamID id) { return feedback[id].empty() ? -1.0 : feedback[id].back().second; };
  std::printf("  Rückmeldungen für %zu Parameter, P11 OCTAVE zuletzt %.2f, P71 CUTOFF %.3f\n", feedback.size(), last(11), last(71));
  check(!feedback.empty(), "Firmware meldet Werte des Editierpuffers");
  check(std::fabs(last(11) - 1.0) < 1e-9, "nach Programm 22 meldet P11 OCTAVE = HIGH");
  bool fight = false;
  for (auto& p : feedback[71]) if (p.first > 3.2 && p.first < 3.5 && std::fabs(p.second - 10 / 63.0) > 1e-9) fight = true;
  check(!fight, "keine Gegenmeldung zum eigenen CUTOFF-Wert");
  check(feedback.count(202) == 0, "Programmwechsel vom Host wird nicht zurückgemeldet");

  std::printf("Zustand:\n");
  MemoryStream st;
  check(comp->getState(&st) == kResultOk, "getState");
  int64 size = 0; st.tell(&size);
  check(size == 4 + 4 + 2048 + 4 + 4 + 8 + 4, "Größe des Zustands (RAM 2 KB + Seriennummer + Volume + Instanzen)");
  st.seek(0, IBStream::kIBSeekSet, nullptr);
  check(comp->setState(&st) == kResultOk, "setState");
  st.seek(0, IBStream::kIBSeekSet, nullptr);
  check(ctrl->setComponentState(&st) == kResultOk, "Controller liest den Zustand");

  comp->setActive(false);
  data.unprepare();
  provider = nullptr;

  // hosts that skip terminate() (or call initialize() twice) during the plugin scan:
  // before 1.0.1 the running writer thread ended the whole process here
  {
    auto raw = factory.createInstance<IComponent>(info.ID());
    check(raw && raw->initialize(static_cast<FUnknown*>(&host)) == kResultOk, "Scan: initialize");
    if (raw) raw->initialize(static_cast<FUnknown*>(&host));
    raw = nullptr;                                   // released without terminate()
    check(true, "Scan: Freigabe ohne terminate() ohne Absturz");
  }
  PluginContextFactory::instance().setPluginContext(nullptr);
  if (argc > 2) { writeWav(argv[2], out); std::printf("geschrieben: %s\n", argv[2]); }
  std::printf(failures ? "FEHLER: %d Prüfungen\n" : "alle Prüfungen ok\n", failures);
  return failures ? 1 : 0;
}
