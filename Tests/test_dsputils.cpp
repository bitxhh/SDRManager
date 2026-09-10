#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "DspUtils.h"

#include <cmath>
#include <complex>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// Magnitude of a real FIR's frequency response at normalised frequency f (=fc/fs).
static double firMag(const std::vector<double>& h, double fNorm)
{
    const double omega = 2.0 * kPi * fNorm;
    double re = 0.0, im = 0.0;
    for (int n = 0; n < static_cast<int>(h.size()); ++n) {
        re += h[n] * std::cos(omega * n);
        im -= h[n] * std::sin(omega * n);
    }
    return std::sqrt(re * re + im * im);
}

// ─────────────────────────────────────────────────────────────────────────────
// designHilbertFir — Type III antisymmetric, zero centre tap
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Hilbert FIR is antisymmetric with zero centre tap", "[dsp][hilbert]") {
    const int taps = 127;
    const auto h = dsp::designHilbertFir(taps);
    REQUIRE(static_cast<int>(h.size()) == taps);

    const int mid = (taps - 1) / 2;
    CHECK_THAT(h[mid], WithinAbs(0.0, 1e-12));

    for (int k = 1; k <= mid; ++k)
        CHECK_THAT(h[mid + k], WithinAbs(-h[mid - k], 1e-12));
}

// ─────────────────────────────────────────────────────────────────────────────
// designHilbertFir — quadrature property: id² + hq² is constant
//   For a cosine, DelayLine(mid) gives cos(w(n-mid)) and the Hilbert gives
//   sin(w(n-mid)); their squares sum to the input amplitude squared.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Hilbert FIR produces a 90° phase shift (unit envelope)", "[dsp][hilbert]") {
    const int taps = 127;
    const int mid  = (taps - 1) / 2;
    dsp::FirReal   hilb;   hilb.setCoeffs(dsp::designHilbertFir(taps));
    dsp::DelayLine dly;    dly.setDelay(mid);

    const double fNorm = 0.10;                 // mid-band, well away from edges
    const double w     = 2.0 * kPi * fNorm;

    double worst = 0.0;
    for (int n = 0; n < 4000; ++n) {
        const double x  = std::cos(w * n);
        const double id = dly.process(x);
        const double hq = hilb.process(x);
        if (n > 400) {                         // skip filter warmup
            const double env = id * id + hq * hq;
            worst = std::max(worst, std::abs(env - 1.0));
        }
    }
    INFO("Worst |env-1|: " << worst);
    CHECK(worst < 0.05);
}

// ─────────────────────────────────────────────────────────────────────────────
// designBandpassFir — unity at centre, strong DC rejection
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Bandpass FIR peaks at centre and rejects DC", "[dsp][bandpass]") {
    const int    taps       = 127;
    const double centerNorm = 0.15;
    const double halfBwNorm = 0.03;
    const auto h = dsp::designBandpassFir(taps, centerNorm, halfBwNorm);
    REQUIRE(static_cast<int>(h.size()) == taps);

    const double magCenter = firMag(h, centerNorm);
    const double magDc     = firMag(h, 0.0);
    const double magFar    = firMag(h, 0.40);

    INFO("|H(center)|=" << magCenter << "  |H(0)|=" << magDc
         << "  |H(0.40)|=" << magFar);

    CHECK_THAT(magCenter, WithinAbs(1.0, 0.1));   // passband ~unity
    CHECK(magDc  < 0.05);                          // DC well rejected
    CHECK(magFar < 0.05);                          // far stopband rejected
}

// ─────────────────────────────────────────────────────────────────────────────
// FirReal — impulse response equals the coefficient vector, in order
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("FirReal impulse response returns coefficients in order", "[dsp][fir]") {
    std::vector<double> c{0.1, -0.2, 0.3, 0.4, -0.5};
    dsp::FirReal fir; fir.setCoeffs(c);

    for (int k = 0; k < static_cast<int>(c.size()); ++k) {
        const double x   = (k == 0) ? 1.0 : 0.0;
        const double out = fir.process(x);
        CHECK_THAT(out, WithinAbs(c[k], 1e-12));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DelayLine — integer delay places the impulse exactly d samples later
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("DelayLine delays an impulse by d samples", "[dsp][delay]") {
    const int d = 7;
    dsp::DelayLine dly; dly.setDelay(d);

    for (int n = 0; n < 20; ++n) {
        const double x   = (n == 0) ? 1.0 : 0.0;
        const double out = dly.process(x);
        if (n == d) CHECK_THAT(out, WithinAbs(1.0, 1e-12));
        else        CHECK_THAT(out, WithinAbs(0.0, 1e-12));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FirComplexDecimator — emits exactly one output per `decim` inputs
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("FirComplexDecimator produces N/decim outputs", "[dsp][decimator]") {
    const int decim = 5;
    dsp::FirComplexDecimator dec;
    dec.setup(dsp::designLowpassFir(31, 0.05), decim);

    int outputs = 0;
    const int N = 1000;
    std::complex<double> out;
    for (int n = 0; n < N; ++n)
        if (dec.process(std::complex<double>(1.0, 0.0), out)) ++outputs;

    CHECK(outputs == N / decim);
}

// ─────────────────────────────────────────────────────────────────────────────
// CarrierPll — locks to a complex tone: bb → real amplitude, imag → 0
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("CarrierPll locks to an offset carrier", "[dsp][pll]") {
    constexpr double fs        = 48'000.0;
    constexpr double offsetHz  = 200.0;
    constexpr double amplitude = 1.0;
    const double w0 = 2.0 * kPi * offsetHz / fs;

    dsp::CarrierPll pll;
    pll.setLoopBandwidth(500.0, fs);
    pll.setFreqLimit(0.5);

    const int N = 40'000;
    double sumRe = 0.0, sumIm = 0.0;
    int    count = 0;
    for (int n = 0; n < N; ++n) {
        const std::complex<double> in(amplitude * std::cos(w0 * n),
                                      amplitude * std::sin(w0 * n));
        const std::complex<double> bb = pll.process(in);
        if (n > N / 2) { sumRe += bb.real(); sumIm += bb.imag(); ++count; }
    }
    const double meanRe = sumRe / count;
    const double meanIm = sumIm / count;

    INFO("mean bb.real=" << meanRe << "  mean bb.imag=" << meanIm
         << "  pll.freq=" << pll.freq << "  w0=" << w0);

    CHECK_THAT(meanRe,   WithinAbs(amplitude, 0.05));
    CHECK_THAT(meanIm,   WithinAbs(0.0,       0.05));
    CHECK_THAT(pll.freq, WithinAbs(w0,        w0 * 0.05));
}
