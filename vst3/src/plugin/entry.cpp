#include "ids.h"
#include "processor.h"
#include "controller.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF("BecauseOfReasons", "", "")

  DEF_CLASS2(INLINE_UID_FROM_FUID(tiracon::ProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
             "Tiracon", 0, PlugType::kInstrumentSynth, "1.0.1", kVstVersionString,
             tiracon::Processor::createInstance)

  DEF_CLASS2(INLINE_UID_FROM_FUID(tiracon::ControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
             "Tiracon Controller", 0, "", "1.0.1", kVstVersionString,
             tiracon::Controller::createInstance)

END_FACTORY
