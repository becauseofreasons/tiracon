#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "multiengine.h"
#include <array>
#include "state.h"
#include "bridge.h"
#include "settings.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace tiracon {

class Processor : public Steinberg::Vst::AudioEffect {
public:
  Processor();
  ~Processor() override;
  static Steinberg::FUnknown* createInstance(void*) { return (Steinberg::Vst::IAudioProcessor*)new Processor; }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                   Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
  std::unique_ptr<t6v::MultiEngine> engine;
  std::vector<float> mono;
  UiBridge* bridge = nullptr;
  UiSnapshot ui;                               // filled on the audio thread
  double sampleRate = 48000;
  int64_t uiCountdown = 0;
  // state exchange with the host thread (the audio thread only uses try_lock)
  std::mutex mtx;
  PluginState snapshot;                       // latest RAM copy for getState
  PluginState pending;
  bool hasPending = false;
  std::unique_ptr<std::array<t6v::DeviceProfile, t6v::MultiEngine::MAX>> pendingDevice;
  std::atomic<int> instances{ 1 };
  std::atomic<double> volume{ 0.7 };
  // standard RAM file: WRITE goes back into it (background thread, no file access on the audio thread)
  std::thread writer;
  std::mutex writerMtx;
  std::condition_variable writerCv;
  bool writerStop = false;
  std::atomic<bool> regionReset{ true };
  Settings settings;

  void handleParams(Steinberg::Vst::IParameterChanges* changes);
  void handleEvent(const Steinberg::Vst::Event& e);
  void outputChanges(Steinberg::Vst::IParameterChanges* out);
  void applyDevice(const std::string& serial);
  void writerLoop();
  void stopWriter();
};

} // namespace tiracon
