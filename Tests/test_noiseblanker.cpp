#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "DspUtils.h"
#include "NfmModem.h"

#include <cmath>
#include <complex>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using cd = std::complex<double>;

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kSR = 2'000'000.0;

// 50 kHz complex tone, amplitude 0.5, plus a little Gaussian noise.
static std::vector<cd> makeTone(int n, double noiseSigma = 0.02) {
    std::mt19937 rng(42);
    std::normal_distribution<double> g(0.0, noiseSigma);
    std::vector<cd> x(n);
    for (int i = 0; i < n; ++i)
        x[i] = std::polar(0.5, 2.0 * kPi * 50'000.0 * i / kSR) + cd{g(rng), g(rng)};
    return x;
}

// Short strong impulses: 3 samples of amplitude 20 every 2000 samples.
static std::vector<cd> addImpulses(std::vector<cd> x, int firstAt) {
    for (int i = firstAt; i + 3 < static_cast<int>(x.size()); i += 2000)
        for (int k = 0; k < 3; ++k)
            x[i + k] += cd{20.0, -15.0};
    return x;
}

static std::vector<cd> run(dsp::NoiseBlanker& nb, const std::vector<cd>& x) {
    std::vector<cd> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = nb.process(x[i]);
    return y;
}

// Error power of y against reference x delayed by d, from sample `from`.
static double errPower(const std::vector<cd>& y, const std::vector<cd>& ref, int d, int from) {
    double e = 0.0;
    for (size_t i = from; i < y.size(); ++i) e += std::norm(y[i] - ref[i - d]);
    return e;
}

TEST_CASE("NoiseBlanker: threshold 0 is a transparent bypass", "[noiseblanker]") {
    dsp::NoiseBlanker nb;
    nb.configure(kSR, 0.0, 20e-6);
    CHECK_FALSE(nb.enabled());
    CHECK(nb.delaySamples() == 0);
    const auto x = addImpulses(makeTone(20'000), 15'000);
    const auto y = run(nb, x);
    for (size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
}

TEST_CASE("NoiseBlanker: clean tone passes undistorted (pure delay)", "[noiseblanker]") {
    dsp::NoiseBlanker nb;
    nb.configure(kSR, 10.0, 20e-6);
    REQUIRE(nb.enabled());
    const int d = nb.delaySamples();
    REQUIRE(d >= 2);

    const auto x = makeTone(100'000);
    const auto y = run(nb, x);
    CHECK(nb.blankedCount() == 0);
    for (size_t i = d; i < x.size(); ++i) {
        REQUIRE_THAT(y[i].real(), WithinAbs(x[i - d].real(), 1e-12));
        REQUIRE_THAT(y[i].imag(), WithinAbs(x[i - d].imag(), 1e-12));
    }
}

TEST_CASE("NoiseBlanker: impulses on a tone are removed, SNR improves", "[noiseblanker]") {
    dsp::NoiseBlanker nb;
    nb.configure(kSR, 10.0, 20e-6);
    const int d = nb.delaySamples();

    const auto clean = makeTone(200'000);
    const auto dirty = addImpulses(clean, 20'001);   // after the 5 ms warm-up
    const auto y = run(nb, dirty);

    const int from = 20'000;
    double sig = 0.0;
    for (size_t i = from; i < clean.size(); ++i) sig += std::norm(clean[i]);

    const double before = errPower(dirty, clean, 0, from);
    const double after  = errPower(y, clean, d, from);
    const double snrBefore = 10.0 * std::log10(sig / before);
    const double snrAfter  = 10.0 * std::log10(sig / after);
    INFO("SNR before " << snrBefore << " dB, after " << snrAfter << " dB");
    CHECK(snrAfter - snrBefore > 15.0);
    CHECK(nb.blankedCount() > 0);

    // Away from impulses the tone is untouched: check the middle of each gap.
    for (int k = from + 1000; k + d < static_cast<int>(clean.size()); k += 2000)
        REQUIRE(std::abs(y[k + d] - clean[k]) < 1e-12);
}

TEST_CASE("NoiseBlanker: modem accepts NB params via setCommonParam", "[noiseblanker]") {
    NfmModem dem(kSR, 0.0);
    CHECK_FALSE(dem.noiseBlanker().enabled());
    CHECK(dem.setCommonParam(kNbThresholdKey, 8.0));
    CHECK(dem.setCommonParam(kNbWidthKey, 30.0));
    CHECK(dem.noiseBlanker().enabled());
    CHECK_FALSE(dem.setCommonParam("Bandwidth", 12'500.0));   // left for applyParam
    CHECK(dem.setCommonParam(kNbThresholdKey, 0.0));
    CHECK_FALSE(dem.noiseBlanker().enabled());
}
