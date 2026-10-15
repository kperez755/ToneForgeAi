#include "tfcore/dsp/modules/NoiseGate.h"

#include <cmath>

namespace tfcore {

float NoiseGate::dbToLinear(float db) noexcept {
    return std::pow(10.0f, db / 20.0f);
}

float NoiseGate::timeToCoeff(float milliseconds) const noexcept {
    // Standard one-pole time-constant coefficient: coeff = 1 - exp(-1 / (T * fs))
    const float timeSeconds = milliseconds / 1000.0f;
    if (timeSeconds <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-1.0f / (timeSeconds * static_cast<float>(sampleRate_)));
}

} // namespace tfcore
