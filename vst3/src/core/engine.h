// Engine: board + sound model + everything the plugin adds on top (no VST3 SDK in here):
// notes, CCs and pitch bend into the emulated MIDI input, throttling, program selection,
// feedback of the firmware's edit buffer to the host, sample-rate bridging.
#pragma once
#include "board.h"
#include "synth.h"
#include "device.h"
#include "firmware.h"
#include <memory>
#include <vector>

namespace t6v {

struct ParamInfo;
struct ParamDef {
  int no;              // firmware parameter number (= VST3 parameter id = MIDI CC number)
  int max;             // values 0..max
  const ParamInfo* info;
};
const std::vector<ParamDef>& firmwareParams();     // parameters 11-76 of the built-in firmware (automatable)
const RomInfo& romInfo();

class Engine {
public:
  static constexpr int PROGRAMS = 32;
  static constexpr int64_t CC_WINDOW = CPU_HZ * 45 / 1000;     // at most one CC per parameter every 45 ms
  static constexpr int64_t PB_WINDOW = CPU_HZ * 10 / 1000;     // pitch bend every 10 ms
  static constexpr int64_t ECHO_HOLD = CPU_HZ * 150 / 1000;    // feedback pause after sending a value
  static constexpr int64_t POLL = CPU_HZ * 20 / 1000;          // read the edit buffer every 20 ms

  struct Change { int no; int value; };            // no = parameter number, or -1 for the program index

  Engine();
  void prepare(double sampleRate);                // allocates: not on the audio thread

  // audio thread
  void noteOn(int pitch, float velocity);
  void noteOff(int pitch);
  void hostParam(int no, int value);
  void resendParam(int no, int value) { if (no >= 0 && no < 100) ps[no].sent = -1; hostParam(no, value); }   // even if sent before
  void hostPitchBend(double normalized);
  void hostProgram(int index);                    // 0..31 = programs 11..48
  void setVolume(double v) { volume = v; if (synth) synth->master = v; }
  void render(float* out, int n);                 // mono
  int takeChanges(Change* out, int max);          // edit buffer changes for the host

  // state (caller makes sure the audio thread is not inside the engine)
  const uint8_t* ram() const { return board->ram; }
  // Power on. The firmware always boots into program 11; loadRam and setDevice then select the program from
  // the RAM again and restore its edit buffer (unsaved changes), so a project comes back as it was saved.
  void loadRam(const uint8_t* ram2k);             // power on with this RAM
  void setDevice(const DeviceProfile& d);         // power on with these components
  void powerOn();                                 // plain reset: stays in program 11
  Board& hw() { return *board; }
  double takePeak() { if (!synth) return 0; double p = synth->peak; synth->peak = 0; return p; }
  const std::string& serial() const { return device.serial; }

private:
  struct ParamState { int want = -1, sent = -1, fwLast = -1; int64_t lastAt = INT64_MIN / 2; bool pending = false; };
  std::unique_ptr<Board> board;
  std::unique_ptr<Synth> synth;
  DeviceProfile device;
  bool hasDevice = false;
  double fs = 48000, cycAcc = 0, volume = 0.7;
  ParamState ps[100];
  int pbWant = 8192, pbSent = 8192;
  int64_t pbLastAt = INT64_MIN / 2;
  bool pbPending = false;
  int progWant = -1, progFw = -1, progPending = -1;
  int64_t progAt = 0;
  int64_t nextPoll = 0;
  static constexpr int64_t RESTORE_BOOT = int64_t(CPU_HZ) * 1200 / 1000;   // firmware ready for MIDI after power on
  static constexpr int64_t RESTORE_EDIT = int64_t(CPU_HZ) * 150 / 1000;   // edit buffer after the program change
  int restoreStage = 0;                            // 0 none, 1 waiting for the boot, 2 waiting for the program change
  int restoreProg = -1;
  int64_t restoreAt = 0;
  int restoreVal[100];
  Change changes[128];
  int nChanges = 0;
  float frame[FRAME_LEN];

  int channel() const;
  void send(const uint8_t* b, int n) { board->midiIn(b, n); }
  void sendParam(int no, int v);
  void sendPitchBend(int v);
  void service();                                  // throttled sends, deferred program change, feedback
  void syncFromRam();
  void powerOnRestoring();
  void pushChange(int no, int v) { if (nChanges < 128) changes[nChanges++] = { no, v }; }
};

} // namespace t6v
