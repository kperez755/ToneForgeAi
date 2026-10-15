#pragma once

#include <cmath>

namespace tfcore {

// M_PI is a POSIX/glibc extension, not standard C++ and not reliably
// defined on MSVC without _USE_MATH_DEFINES - defining our own constant
// avoids a repeat of the gmtime_r/strptime/timegm portability bug (see
// core/src/preset/PresetSerializer.cpp) that only surfaced on a real
// Windows build.
inline constexpr double kPi = 3.14159265358979323846;

// Standard RBJ ("Audio EQ Cookbook") biquad filter, direct form I.
// Shared by every module that needs tone shaping (Amp's tone stack, Cab's
// cabinet-response approximation, Eq) rather than each reimplementing its
// own filter math - one implementation to get right and test, several
// modules that trust it.
//
// Not RT-unsafe by construction: process() only does arithmetic on
// already-owned state, no allocation. Coefficient recalculation
// (setLowShelf/setHighShelf/setPeaking) does a handful of trig calls
// (sin/cos/pow) - cheap enough to call once per block when a parameter
// changes, but callers should not call it needlessly every sample.
class Biquad {
public:
    void reset() noexcept { x1_ = x2_ = y1_ = y2_ = 0.0f; }

    // process() a single sample through the filter - RT-safe.
    [[nodiscard]] float process(float x) noexcept {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

    void setLowShelf(double sampleRate, double freqHz, double gainDb, double q = 0.707) noexcept {
        setShelf(sampleRate, freqHz, gainDb, q, /*isLowShelf=*/true);
    }

    void setHighShelf(double sampleRate, double freqHz, double gainDb, double q = 0.707) noexcept {
        setShelf(sampleRate, freqHz, gainDb, q, /*isLowShelf=*/false);
    }

    void setPeaking(double sampleRate, double freqHz, double gainDb, double q) noexcept {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * freqHz / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cosw0 = std::cos(w0);

        const double b0 = 1.0 + alpha * a;
        const double b1 = -2.0 * cosw0;
        const double b2 = 1.0 - alpha * a;
        const double a0 = 1.0 + alpha / a;
        const double a1 = -2.0 * cosw0;
        const double a2 = 1.0 - alpha / a;

        normalizeAndStore(b0, b1, b2, a0, a1, a2);
    }

    void setHighPass(double sampleRate, double freqHz, double q = 0.707) noexcept {
        const double w0 = 2.0 * kPi * freqHz / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cosw0 = std::cos(w0);

        const double b0 = (1.0 + cosw0) / 2.0;
        const double b1 = -(1.0 + cosw0);
        const double b2 = (1.0 + cosw0) / 2.0;
        const double a0 = 1.0 + alpha;
        const double a1 = -2.0 * cosw0;
        const double a2 = 1.0 - alpha;

        normalizeAndStore(b0, b1, b2, a0, a1, a2);
    }

    void setLowPass(double sampleRate, double freqHz, double q = 0.707) noexcept {
        const double w0 = 2.0 * kPi * freqHz / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double cosw0 = std::cos(w0);

        const double b0 = (1.0 - cosw0) / 2.0;
        const double b1 = 1.0 - cosw0;
        const double b2 = (1.0 - cosw0) / 2.0;
        const double a0 = 1.0 + alpha;
        const double a1 = -2.0 * cosw0;
        const double a2 = 1.0 - alpha;

        normalizeAndStore(b0, b1, b2, a0, a1, a2);
    }

private:
    void setShelf(double sampleRate, double freqHz, double gainDb, double q, bool isLowShelf) noexcept {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * freqHz / sampleRate;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / q - 1.0) + 2.0);
        const double twoSqrtAAlpha = 2.0 * std::sqrt(a) * alpha;

        double b0, b1, b2, a0, a1, a2;
        if (isLowShelf) {
            b0 = a * ((a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosw0);
            b2 = a * ((a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha);
            a0 = (a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosw0);
            a2 = (a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha;
        } else {
            b0 = a * ((a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw0);
            b2 = a * ((a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha);
            a0 = (a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw0);
            a2 = (a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha;
        }

        normalizeAndStore(b0, b1, b2, a0, a1, a2);
    }

    void normalizeAndStore(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
        b0_ = static_cast<float>(b0 / a0);
        b1_ = static_cast<float>(b1 / a0);
        b2_ = static_cast<float>(b2 / a0);
        a1_ = static_cast<float>(a1 / a0);
        a2_ = static_cast<float>(a2 / a0);
    }

    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
};

} // namespace tfcore
