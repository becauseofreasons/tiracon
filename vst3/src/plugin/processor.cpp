#include "processor.h"
#include "ids.h"
#include "snapshot.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <algorithm>
#include <chrono>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace tiracon {

namespace {
// RAM parts that WRITE changes: MAST. TUNE + 32 programs, and the extension table of the mod build
bool sameStored(const uint8_t* a, const uint8_t* b) {
  return std::memcmp(a + 0x187, b + 0x187, 0x408 - 0x187) == 0 && std::memcmp(a + 0x460, b + 0x460, 0x5C4 - 0x460) == 0;
}
}

Processor::Processor() {
  setControllerClass(ControllerUID);
  engine = std::make_unique<t6v::MultiEngine>();
  // new instance: start with the standard RAM file if there is one, else the factory programs
  settings = Settings::load();
  uint8_t ram[2048];
  if (!settings.ramDefault.empty() && loadRamFile(settings.ramDefault, ram)) engine->loadRam(ram);
  std::memcpy(snapshot.ram, engine->primary().ram(), sizeof snapshot.ram);
  mono.assign(4096, 0.0f);
  engine->prepare(48000, 4096);
  bridge = new UiBridge;
}

// some hosts release the component without terminate(): stop the writer here too,
// a still running std::thread would end the whole process (std::terminate)
Processor::~Processor() {
  stopWriter();
  if (bridge) bridge->release();
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
  tresult r = AudioEffect::initialize(context);
  if (r != kResultOk) return r;
  addAudioOutput(STR16("Out"), SpeakerArr::kStereo);
  addEventInput(STR16("MIDI In"), 16);
  if (!writer.joinable()) {                   // initialize() twice: keep the running thread
    writerStop = false;
    writer = std::thread([this] { writerLoop(); });
  }
  return kResultOk;
}

void Processor::stopWriter() {
  {
    std::lock_guard<std::mutex> l(writerMtx);
    writerStop = true;
  }
  writerCv.notify_all();
  if (writer.joinable()) writer.join();
}

tresult PLUGIN_API Processor::terminate() {
  stopWriter();
  return AudioEffect::terminate();
}

// hand the bridge to the controller (same process)
tresult PLUGIN_API Processor::connect(IConnectionPoint* other) {
  tresult r = AudioEffect::connect(other);
  if (r == kResultOk && bridge) {
    if (auto msg = owned(allocateMessage())) {
      msg->setMessageID("bridge");
      msg->getAttributes()->setInt("ptr", int64(reinterpret_cast<intptr_t>(bridge)));
      sendMessage(msg);
    }
  }
  return r;
}

void Processor::applyDevice(const std::string& serial) {
  auto dev = std::make_unique<std::array<t6v::DeviceProfile, t6v::MultiEngine::MAX>>(t6v::MultiEngine::makeDevices(serial));   // allocates: host thread
  std::lock_guard<std::mutex> lock(mtx);
  pendingDevice = std::move(dev);
  snapshot.serial = serial;
}

// messages from the controller (host thread)
tresult PLUGIN_API Processor::notify(IMessage* message) {
  if (!message) return kInvalidArgument;
  FIDString id = message->getMessageID();
  if (FIDStringsEqual(id, "ram")) {
    const void* data = nullptr;
    uint32 size = 0;
    if (message->getAttributes()->getBinary("data", data, size) == kResultOk && size == 2048) {
      std::lock_guard<std::mutex> lock(mtx);
      pending = snapshot;
      std::memcpy(pending.ram, data, 2048);
      hasPending = true;
      regionReset = true;
    }
    return kResultOk;
  }
  if (FIDStringsEqual(id, "device")) {
    const void* data = nullptr;
    uint32 size = 0;
    if (message->getAttributes()->getBinary("serial", data, size) == kResultOk && size > 0 && size < 24)
      applyDevice(std::string(static_cast<const char*>(data), size));
    return kResultOk;
  }
  if (FIDStringsEqual(id, "settings")) {
    std::lock_guard<std::mutex> l(writerMtx);
    settings = Settings::load();
    regionReset = true;
    return kResultOk;
  }
  return AudioEffect::notify(message);
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
  if (numIns == 0 && numOuts == 1 && (outputs[0] == SpeakerArr::kStereo || outputs[0] == SpeakerArr::kMono))
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
  return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
  return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
  std::lock_guard<std::mutex> lock(mtx);        // inactive: the audio thread does not run
  engine->prepare(setup.sampleRate, std::max<int32>(setup.maxSamplesPerBlock, 32));
  sampleRate = setup.sampleRate > 0 ? setup.sampleRate : 48000;
  mono.assign(size_t(std::max<int32>(setup.maxSamplesPerBlock, 32)), 0.0f);
  return AudioEffect::setupProcessing(setup);
}

void Processor::handleParams(IParameterChanges* changes) {
  if (!changes) return;
  int32 n = changes->getParameterCount();
  for (int32 i = 0; i < n; i++) {
    IParamValueQueue* q = changes->getParameterData(i);
    if (!q || q->getPointCount() <= 0) continue;
    int32 offset;
    ParamValue v;
    if (q->getPoint(q->getPointCount() - 1, offset, v) != kResultTrue) continue;
    ParamID id = q->getParameterId();
    if (id == kParamVolume) { volume = v; engine->setVolume(v); }
    else if (id == kParamPitchBend) engine->hostPitchBend(v);
    else if (id == kParamProgram) engine->hostProgram(std::min(31, int(v * 32)));
    else if (id >= 11 && id <= 76 && t6v::romInfo().has(int(id))) {
      int max = t6v::romInfo().max[id];
      engine->hostParam(int(id), std::min(max, int(v * (max + 1))));      // VST3 discrete mapping
    }
  }
}

void Processor::handleEvent(const Event& e) {
  if (e.type == Event::kNoteOnEvent) {
    if (e.noteOn.velocity <= 0) engine->noteOff(e.noteOn.pitch);
    else engine->noteOn(e.noteOn.pitch, e.noteOn.velocity);
  } else if (e.type == Event::kNoteOffEvent) {
    engine->noteOff(e.noteOff.pitch);
  }
}

void Processor::outputChanges(IParameterChanges* out) {
  t6v::Engine::Change ch[128];
  int n = engine->takeChanges(ch, 128);
  if (!out) return;
  for (int i = 0; i < n; i++) {
    ParamID id;
    ParamValue v;
    if (ch[i].no < 0) { id = kParamProgram; v = ch[i].value / 31.0; }
    else { id = ParamID(ch[i].no); int max = t6v::romInfo().max[ch[i].no]; v = max > 0 ? double(ch[i].value) / max : 0; }
    int32 idx = 0;
    IParamValueQueue* q = out->addParameterData(id, idx);
    if (q) q->addPoint(0, v, idx);
  }
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
  // state from the host thread (project loaded, RAM file, device changed)
  if (mtx.try_lock()) {
    if (pendingDevice) { engine->setDevices(*pendingDevice); pendingDevice.reset(); }
    if (hasPending) { engine->setCount(int(pending.instances)); engine->loadRam(pending.ram); hasPending = false; }
    std::memcpy(snapshot.ram, engine->primary().ram(), sizeof snapshot.ram);
    snapshot.instances = uint32_t(engine->count());
    snapshot.volume = volume;
    mtx.unlock();
  }
  engine->setVolume(volume);
  handleParams(data.inputParameterChanges);
  // editor commands
  UiCommand cmd;
  while (bridge && bridge->pop(cmd)) {
    switch (cmd.type) {
      case UiCommand::BlpKey: engine->key(cmd.a, cmd.b != 0); break;
      case UiCommand::Note: engine->testNote(cmd.a, cmd.b != 0); break;
      case UiCommand::Instances: engine->setCount(cmd.a); instances = engine->count(); break;
      case UiCommand::Param: engine->hostParam(cmd.a, cmd.b); break;
      case UiCommand::Reset: engine->powerOn(); break;
    }
  }

  int32 n = data.numSamples;
  if (n > int32(mono.size())) n = int32(mono.size());          // never happens with a correct host
  int32 pos = 0;
  if (IEventList* ev = data.inputEvents) {
    int32 count = ev->getEventCount();
    for (int32 i = 0; i < count; i++) {
      Event e;
      if (ev->getEvent(i, e) != kResultOk) continue;
      int32 at = std::max<int32>(pos, std::min<int32>(e.sampleOffset, n));
      if (at > pos) { engine->render(mono.data() + pos, at - pos); pos = at; }
      handleEvent(e);
    }
  }
  if (n > pos) engine->render(mono.data() + pos, n - pos);
  outputChanges(data.outputParameterChanges);

  // editor snapshot about 60x per second
  uiCountdown -= n;
  if (bridge && uiCountdown <= 0) {
    uiCountdown = int64_t(sampleRate / 60);
    fillSnapshot(*engine, ui, engine->serial());
    bridge->tryPublish(ui);
  }

  if (data.numOutputs > 0 && n > 0) {
    AudioBusBuffers& bus = data.outputs[0];
    for (int32 c = 0; c < bus.numChannels; c++) {
      if (bus.channelBuffers32 && bus.channelBuffers32[c]) std::memcpy(bus.channelBuffers32[c], mono.data(), size_t(n) * sizeof(float));
    }
    bus.silenceFlags = 0;
  }
  return kResultOk;
}

// background: programs saved with WRITE (or MAST. TUNE) go into the standard RAM file
void Processor::writerLoop() {
  uint8_t last[2048], cur[2048];
  bool haveLast = false;
  std::unique_lock<std::mutex> lk(writerMtx);
  while (!writerStop) {
    writerCv.wait_for(lk, std::chrono::milliseconds(700));
    if (writerStop) break;
    Settings s = settings;
    lk.unlock();
    {
      std::lock_guard<std::mutex> lock(mtx);
      std::memcpy(cur, hasPending ? pending.ram : snapshot.ram, sizeof cur);
    }
    if (regionReset.exchange(false)) haveLast = false;
    if (haveLast && !sameStored(last, cur) && s.autoWrite && !s.ramDefault.empty()) writeFileAtomic(s.ramDefault, cur, 2048);
    std::memcpy(last, cur, sizeof last);
    haveLast = true;
    lk.lock();
  }
}

tresult PLUGIN_API Processor::setState(IBStream* stream) {
  PluginState st;
  if (!st.read(stream)) return kResultFalse;
  std::unique_ptr<std::array<t6v::DeviceProfile, t6v::MultiEngine::MAX>> dev;
  bool newSerial;
  {
    std::lock_guard<std::mutex> lock(mtx);
    newSerial = st.serial != snapshot.serial;
  }
  if (newSerial) dev = std::make_unique<std::array<t6v::DeviceProfile, t6v::MultiEngine::MAX>>(t6v::MultiEngine::makeDevices(st.serial));   // allocates, outside the lock
  std::lock_guard<std::mutex> lock(mtx);
  pending = st;
  hasPending = true;
  if (dev) pendingDevice = std::move(dev);
  snapshot = st;
  volume = st.volume;
  instances = int(st.instances);
  regionReset = true;
  return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* stream) {
  PluginState st;
  {
    std::lock_guard<std::mutex> lock(mtx);
    st = hasPending ? pending : snapshot;
    st.volume = volume;
    if (!hasPending) st.instances = uint32_t(instances.load());
  }
  return st.write(stream) ? kResultOk : kResultFalse;
}

} // namespace tiracon
