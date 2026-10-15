#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/Biquad.h"

using namespace tfcore;

namespace {

// Feeds a steady-state sine wave through a (settled) filter and measures
// output RMS - a practical way to sanity-check filter frequency response
// without a full FFT: run long enough for the transient to die out, then
// measure amplitude at a known frequency.
float measureGainAtFrequency(Biquad& filter, double sampleRate, double freqHz, int numSamples = 4000) {
    double phase = 0.0;
    const double phaseIncrement = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;

    double sumSquares = 0.0;
    int measuredSamples = 0;
    const int warmupSamples = numSamples / 2; // let the filter settle first

    for (int i = 0; i < numSamples; ++i) {
        const float x = static_cast<float>(std::sin(phase));
        const float y = filter.process(x);
        phase += phaseIncrement;
        if (i >= warmupSamples) {
            sumSquares += static_cast<double>(y) * y;
            ++measuredSamples;
        }
    }
    return static_cast<float>(std::sqrt(sumSquares / measuredSamples));
}

} // namespace

TEST_CASE("Biquad low shelf boosts low frequencies more than high frequencies") {
    Biquad filter;
    filter.setLowShelf(44100.0, 200.0, 12.0);
    const float lowGain = measureGainAtFrequency(filter, 44100.0, 80.0);

    filter.reset();
    filter.setLowShelf(44100.0, 200.0, 12.0);
    const float highGain = measureGainAtFrequency(filter, 44100.0, 8000.0);

    REQUIRE(lowGain > highGain);
}

TEST_CASE("Biquad high shelf boosts high frequencies more than low frequencies") {
    Biquad filter;
    filter.setHighShelf(44100.0, 3000.0, 12.0);
    const float highGain = measureGainAtFrequency(filter, 44100.0, 10000.0);

    filter.reset();
    filter.setHighShelf(44100.0, 3000.0, 12.0);
    const float lowGain = measureGainAtFrequency(filter, 44100.0, 100.0);

    REQUIRE(highGain > lowGain);
}

TEST_CASE("Biquad peaking filter boosts near its center frequency more than far from it") {
    Biquad filter;
    filter.setPeaking(44100.0, 1000.0, 12.0, 1.0);
    const float atCenter = measureGainAtFrequency(filter, 44100.0, 1000.0);

    filter.reset();
    filter.setPeaking(44100.0, 1000.0, 12.0, 1.0);
    const float farAway = measureGainAtFrequency(filter, 44100.0, 40.0);

    REQUIRE(atCenter > farAway);
}

TEST_CASE("Biquad zero-gain shelf and peak leave signal roughly unchanged") {
    Biquad filter;
    filter.setPeaking(44100.0, 1000.0, 0.0, 1.0);
    const float gain = measureGainAtFrequency(filter, 44100.0, 1000.0);
    // Steady-state sine RMS is amplitude/sqrt(2) ~= 0.707 for unity gain.
    REQUIRE(std::fabs(gain - 0.7071f) < 0.05f);
}

TEST_CASE("Biquad low pass attenuates high frequencies relative to low frequencies") {
    Biquad filter;
    filter.setLowPass(44100.0, 500.0, 0.707);
    const float lowGain = measureGainAtFrequency(filter, 44100.0, 100.0);

    filter.reset();
    filter.setLowPass(44100.0, 500.0, 0.707);
    const float highGain = measureGainAtFrequency(filter, 44100.0, 8000.0);

    REQUIRE(lowGain > highGain);
}

TEST_CASE("Biquad high pass attenuates low frequencies relative to high frequencies") {
    Biquad filter;
    filter.setHighPass(44100.0, 500.0, 0.707);
    const float highGain = measureGainAtFrequency(filter, 44100.0, 8000.0);

    filter.reset();
    filter.setHighPass(44100.0, 500.0, 0.707);
    const float lowGain = measureGainAtFrequency(filter, 44100.0, 50.0);

    REQUIRE(highGain > lowGain);
}
