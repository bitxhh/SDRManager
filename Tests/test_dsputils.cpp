#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "DspUtils.h"

#include <algorithm>
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

// ─────────────────────────────────────────────────────────────────────────────
// designLowpassFir — windowed-sinc (Blackman), unity DC gain
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Lowpass FIR is symmetric with unity DC gain", "[dsp][lowpass]") {
    const int taps = 127;
    const auto h = dsp::designLowpassFir(taps, 0.1);
    REQUIRE(static_cast<int>(h.size()) == taps);

    double sum = 0.0;
    for (double v : h) sum += v;
    CHECK_THAT(sum, WithinAbs(1.0, 1e-12));

    for (int n = 0; n < taps; ++n)
        REQUIRE_THAT(h[n], WithinAbs(h[taps - 1 - n], 1e-15));
}

TEST_CASE("Lowpass FIR passes the passband and rejects the stopband", "[dsp][lowpass]") {
    const auto h = dsp::designLowpassFir(127, 0.1);
    CHECK_THAT(firMag(h, 0.02), WithinAbs(1.0, 0.01));
    CHECK_THAT(firMag(h, 0.05), WithinAbs(1.0, 0.01));
    CHECK_THAT(firMag(h, 0.1),  WithinAbs(0.5, 0.05));   // -6 dB at cutoff
    CHECK(firMag(h, 0.2)  < 1e-3);                        // > 60 dB rejection
    CHECK(firMag(h, 0.45) < 1e-3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Nco — frequency shift by -offset
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Nco shifts a tone at +offset down to DC", "[dsp][nco]") {
    const double sr = 1.0e6, off = 123.4e3;
    dsp::Nco nco;
    nco.setFrequency(off, sr);

    const double w = 2.0 * kPi * off / sr;
    for (int n = 0; n < 5000; ++n) {
        const std::complex<double> in = std::polar(1.0, w * n);
        const auto out = nco.mix(in);
        REQUIRE_THAT(out.real(), WithinAbs(1.0, 1e-6));
        REQUIRE_THAT(out.imag(), WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("Nco phase stays wrapped to [-pi, pi] over a long run", "[dsp][nco]") {
    dsp::Nco nco;
    nco.setFrequency(-370.0e3, 1.0e6);   // large positive phase increment
    for (int n = 0; n < 1'000'000; ++n) {
        nco.mix({1.0, 0.0});
        REQUIRE(nco.phase <= kPi);
        REQUIRE(nco.phase >= -kPi);
    }
    // Magnitude is preserved even after a million wraps.
    CHECK_THAT(std::abs(nco.mix({0.6, 0.8})), WithinAbs(1.0, 1e-12));

    nco.reset();
    CHECK(nco.phase == 0.0);
    const auto first = nco.mix({1.0, 0.0});
    CHECK_THAT(first.real(), WithinAbs(1.0, 1e-15));
}

// ─────────────────────────────────────────────────────────────────────────────
// IirHighpass1 — first-order audio highpass
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("IirHighpass1 removes DC and passes high frequencies", "[dsp][highpass]") {
    const double fs = 48000.0;
    dsp::IirHighpass1 hp;
    hp.setCutoff(30.0, fs);

    SECTION("step input decays to zero") {
        double y = 0.0;
        for (int n = 0; n < 48000; ++n) y = hp.process(1.0);   // 1 s ≫ τ ≈ 5.3 ms
        CHECK(std::abs(y) < 1e-6);
    }

    SECTION("1 kHz tone passes with ~unity gain") {
        const double w = 2.0 * kPi * 1000.0 / fs;
        double peak = 0.0;
        for (int n = 0; n < 48000; ++n) {
            const double y = hp.process(std::sin(w * n));
            if (n > 24000) peak = std::max(peak, std::abs(y));
        }
        CHECK_THAT(peak, WithinAbs(1.0, 0.01));
    }

    SECTION("reset clears history") {
        for (int n = 0; n < 100; ++n) hp.process(0.7);
        hp.reset();
        dsp::IirHighpass1 fresh;
        fresh.setCutoff(30.0, fs);
        CHECK(hp.process(0.3) == fresh.process(0.3));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DcBlocker — complex DC removal on I/Q
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("DcBlocker removes a complex DC offset but keeps the signal", "[dsp][dcblocker]") {
    dsp::DcBlocker dc;
    const std::complex<double> offset{0.3, -0.2};
    const double w = 2.0 * kPi * 0.05;

    const int N = 100'000;   // ≫ τ = 1/(1-alpha) = 10 000 samples
    std::complex<double> mean{0.0, 0.0};
    double power = 0.0;
    const int tail = 10'000;
    for (int n = 0; n < N; ++n) {
        const auto y = dc.process(offset + std::polar(0.5, w * n));
        if (n >= N - tail) {
            mean  += y;
            power += std::norm(y);
        }
    }
    mean  /= tail;
    power /= tail;

    CHECK(std::abs(mean) < 1e-3);
    CHECK_THAT(power, WithinAbs(0.25, 0.005));   // tone power 0.5² unchanged

    dc.reset();
    CHECK(dc.prevIn == std::complex<double>{});
    CHECK(dc.prevOut == std::complex<double>{});
}
