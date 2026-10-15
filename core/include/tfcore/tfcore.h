#pragma once

// Convenience umbrella header for consumers (the JUCE plugin layer, tests)
// that want "all of core". Internal core/src files should still include
// the specific headers they need rather than this umbrella, to keep
// compile-time dependencies explicit and honest.

#include "tfcore/analysis/IAudioAnalyzer.h"
#include "tfcore/chain/SignalChain.h"
#include "tfcore/dsp/AudioBuffer.h"
#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/Biquad.h"
#include "tfcore/dsp/IAudioModule.h"
#include "tfcore/dsp/ParameterStore.h"
#include "tfcore/dsp/modules/Amp.h"
#include "tfcore/dsp/modules/Cab.h"
#include "tfcore/dsp/modules/Drive.h"
#include "tfcore/dsp/modules/Eq.h"
#include "tfcore/dsp/modules/NoiseGate.h"
#include "tfcore/preset/ModuleState.h"
#include "tfcore/preset/Preset.h"
#include "tfcore/preset/PresetSerializer.h"
#include "tfcore/util/Json.h"
