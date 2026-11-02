// Linux only: VSTGUI's X11 frame needs the host's run loop (Steinberg::Linux::IRunLoop from the IPlugFrame).
// Same adapter as in VSTGUI's vst3editor.cpp, which is not used here.
#pragma once
#if SMTG_OS_LINUX
#include "vstgui/lib/vstguibase.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <vector>

namespace tiracon {

class HostRunLoop : public VSTGUI::IRunLoop, public VSTGUI::AtomicReferenceCounted {
public:
  explicit HostRunLoop(Steinberg::FUnknown* plugFrame) : runLoop(plugFrame) {}

  bool registerEventHandler(int fd, VSTGUI::IEventHandler* handler) final {
    if (!runLoop) return false;
    auto h = Steinberg::owned(new EventHandler());
    h->handler = handler;
    if (runLoop->registerEventHandler(h, fd) != Steinberg::kResultTrue) return false;
    events.push_back(h);
    return true;
  }
  bool unregisterEventHandler(VSTGUI::IEventHandler* handler) final {
    if (!runLoop) return false;
    for (auto it = events.begin(); it != events.end(); ++it)
      if ((*it)->handler == handler) { runLoop->unregisterEventHandler(*it); events.erase(it); return true; }
    return false;
  }
  bool registerTimer(uint64_t interval, VSTGUI::ITimerHandler* handler) final {
    if (!runLoop) return false;
    auto h = Steinberg::owned(new TimerHandler());
    h->handler = handler;
    if (runLoop->registerTimer(h, interval) != Steinberg::kResultTrue) return false;
    timers.push_back(h);
    return true;
  }
  bool unregisterTimer(VSTGUI::ITimerHandler* handler) final {
    if (!runLoop) return false;
    for (auto it = timers.begin(); it != timers.end(); ++it)
      if ((*it)->handler == handler) { runLoop->unregisterTimer(*it); timers.erase(it); return true; }
    return false;
  }

private:
  struct EventHandler : Steinberg::Linux::IEventHandler, public Steinberg::FObject {
    VSTGUI::IEventHandler* handler = nullptr;
    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor) override { if (handler) handler->onEvent(); }
    DELEGATE_REFCOUNT(Steinberg::FObject)
    DEFINE_INTERFACES
      DEF_INTERFACE(Steinberg::Linux::IEventHandler)
    END_DEFINE_INTERFACES(Steinberg::FObject)
  };
  struct TimerHandler : Steinberg::Linux::ITimerHandler, public Steinberg::FObject {
    VSTGUI::ITimerHandler* handler = nullptr;
    void PLUGIN_API onTimer() final { if (handler) handler->onTimer(); }
    DELEGATE_REFCOUNT(Steinberg::FObject)
    DEFINE_INTERFACES
      DEF_INTERFACE(Steinberg::Linux::ITimerHandler)
    END_DEFINE_INTERFACES(Steinberg::FObject)
  };
  std::vector<Steinberg::IPtr<EventHandler>> events;
  std::vector<Steinberg::IPtr<TimerHandler>> timers;
  Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> runLoop;
};

} // namespace tiracon
#endif
