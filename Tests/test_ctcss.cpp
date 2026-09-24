#include <catch2/catch_test_macros.hpp>

#include "../DSP/CtcssDetector.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

constexpr double kFs = 50'000.0;   // NFM audio rate
constexpr double kPi = 3.14159265358979323846;

// Sub-audible tone + optional 1 kHz "voice" + white noise, 1.5 s.
std::vector<float> makeAudio(double toneHz, double toneAmp, double voiceAmp,
                             double noiseAmp, double seconds = 1.5) {
    std::mt19937 rng(1234);
    std::normal_distribution<double> nd(0.0, 1.0);
    const int n = static_cast<int>(seconds * kFs);
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i) {
        const double t = i / kFs;
        double v = toneAmp * std::sin(2 * kPi * toneHz * t)
                 + voiceAmp * std::sin(2 * kPi * 1000.0 * t)
                 + noiseAmp * nd(rng);
        x[i] = static_cast<float>(v);
    }
    return x;
}

void runBlocks(CtcssDetector& d, std::vector<float>& x, int block = 1000) {
    for (size_t i = 0; i < x.size(); i += block)
        d.process(x.data() + i, static_cast<int>(std::min<size_t>(block, x.size() - i)));
}

double rms(const std::vector<float>& x, size_t from) {
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(x.size() - from));
}

double sineRms(double hz) {
    CtcssDetector d;
    d.prepare(kFs);
    auto x = makeAudio(hz, 1.0, 0.0, 0.0, 1.0);
    runBlocks(d, x);
    return rms(x, x.size() / 2);
}

} // namespace

TEST_CASE("CtcssDetector: detects standard tones under voice and noise", "[ctcss]") {
    for (double hz : {67.0, 88.5, 162.2, 254.1}) {
        CtcssDetector d;
        d.prepare(kFs);
        // Tone ~15 % of deviation, voice much louder, some noise.
        auto x = makeAudio(hz, 0.15, 0.8, 0.05);
        runBlocks(d, x);
        INFO("tone " << hz);
        CHECK(d.detectedToneHz() == hz);
    }
}

TEST_CASE("CtcssDetector: adjacent tones are resolved", "[ctcss]") {
    for (double hz : {69.3, 71.9, 159.8, 162.2, 165.5}) {
        CtcssDetector d;
        d.prepare(kFs);
        auto x = makeAudio(hz, 0.2, 0.0, 0.02);
        runBlocks(d, x);
        INFO("tone " << hz);
        CHECK(d.detectedToneHz() == hz);
    }
}

TEST_CASE("CtcssDetector: no detection on noise or voice alone", "[ctcss]") {
    SECTION("noise") {
        CtcssDetector d;
        d.prepare(kFs);
        auto x = makeAudio(0.0, 0.0, 0.0, 0.3);
        runBlocks(d, x);
        CHECK(d.detectedToneHz() == 0.0);
    }
    SECTION("1 kHz tone") {
        CtcssDetector d;
        d.prepare(kFs);
        auto x = makeAudio(0.0, 0.0, 0.8, 0.0);
        runBlocks(d, x);
        CHECK(d.detectedToneHz() == 0.0);
    }
}

TEST_CASE("CtcssDetector: detection clears after the tone stops", "[ctcss]") {
    CtcssDetector d;
    d.prepare(kFs);
    auto on = makeAudio(100.0, 0.2, 0.0, 0.02);
    runBlocks(d, on);
    REQUIRE(d.detectedToneHz() == 100.0);
    auto off = makeAudio(0.0, 0.0, 0.0, 0.02);
    runBlocks(d, off);
    CHECK(d.detectedToneHz() == 0.0);
}

TEST_CASE("CtcssDetector: 300 Hz highpass removes the tone, keeps voice", "[ctcss]") {
    const double a100 = 20.0 * std::log10(sineRms(100.0) / (1.0 / std::sqrt(2.0)));
    const double a254 = 20.0 * std::log10(sineRms(254.1) / (1.0 / std::sqrt(2.0)));
    const double a1k  = 20.0 * std::log10(sineRms(1000.0) / (1.0 / std::sqrt(2.0)));
    CHECK(a100 < -40.0);
    CHECK(a254 < -6.0);
    CHECK(std::abs(a1k) < 0.1);
}

TEST_CASE("CtcssDetector: tone squelch", "[ctcss]") {
    SECTION("mismatched tone is muted") {
        CtcssDetector d;
        d.prepare(kFs);
        REQUIRE(d.setParam("CTCSS", 88.5));
        auto x = makeAudio(100.0, 0.15, 0.8, 0.0);
        runBlocks(d, x);
        CHECK(d.detectedToneHz() == 100.0);
        CHECK(rms(x, x.size() / 2) < 1e-6);
    }
    SECTION("matching tone passes") {
        CtcssDetector d;
        d.prepare(kFs);
        REQUIRE(d.setParam("CTCSS", 88.5));
        auto x = makeAudio(88.5, 0.15, 0.8, 0.0);
        runBlocks(d, x);
        CHECK(d.detectedToneHz() == 88.5);
        CHECK(rms(x, x.size() / 2) > 0.5);
    }
    SECTION("off passes everything") {
        CtcssDetector d;
        d.prepare(kFs);
        auto x = makeAudio(0.0, 0.0, 0.8, 0.0);
        runBlocks(d, x);
        CHECK(rms(x, x.size() / 2) > 0.5);
    }
    SECTION("unknown param is rejected") {
        CtcssDetector d;
        CHECK_FALSE(d.setParam("Bandwidth", 12'500.0));
    }
}

TEST_CASE("CtcssDetector: standard tone table", "[ctcss]") {
    const auto& t = CtcssDetector::kTones;
    CHECK(t.front() == 67.0);
    CHECK(t.back() == 254.1);
    for (size_t i = 1; i < t.size(); ++i)
        CHECK(t[i] - t[i - 1] > 2.0);   // resolvable with 2.5 Hz bins
}
