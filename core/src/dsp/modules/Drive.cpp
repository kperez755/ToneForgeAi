#include "tfcore/dsp/modules/Drive.h"

namespace tfcore {

float Drive::dbToLinear(float db) noexcept {
    return std::pow(10.0f, db / 20.0f);
}

} // namespace tfcore
