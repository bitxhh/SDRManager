#include <catch2/catch_test_macros.hpp>

#include "../DSP/DcsDetector.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

constexpr double kFs = 50'000.0;   // NFM audio rate
constexpr double kPi = 3.14159265358979323846;

// DCS NRZ (±amp, LSB first, repeated) through a 1-pole ~300 Hz LPF like a
// radio's, plus "voice" (1 kHz + 2.3 kHz) and white noise. `rateScale`
// detunes the bit clock; `startBit` shifts the word boundary.
std::vector<float> makeAudio(int code, bool inverted, double amp, double voiceAmp,
                             double noiseAmp, double seconds = 1.5,
                             double rateScale = 1.0, double startBit = 7.3) {
    std::mt19937 rng(1234);
    std::normal_distribution<double> nd(0.0, 1.0);
    const uint32_t word = code > 0 ? DcsDetector::encode(code) : 0;
    const double   a    = 1.0 - std::exp(-2 * kPi * 300.0 / kFs);
    const int n = static_cast<int>(seconds * kFs);
    std::vector<float> x(n);
    double lp = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kFs;
        double nrz = 0.0;
        if (code > 0) {
            const long bit = static_cast<long>(std::floor(startBit + t * DcsDetector::kBitRate * rateScale));
            const int  b   = (word >> (bit % 23)) & 1;
            nrz = (b ^ (inverted ? 1 : 0)) ? amp : -amp;
        }
        lp += a * (nrz - lp);
        const double v = lp
                       + voiceAmp * (0.6 * std::sin(2 * kPi * 1000.0 * t) + 0.4 * std::sin(2 * kPi * 2300.0 * t))
                       + noiseAmp * nd(rng);
        x[i] = static_cast<float>(v);
    }
    return x;
}

void runBlocks(DcsDetector& d, std::vector<float>& x, int block = 1000) {
    for (size_t i = 0; i < x.size(); i += block)
        d.process(x.data() + i, static_cast<int>(std::min<size_t>(block, x.size() - i)));
}

double rms(const std::vector<float>& x, size_t from) {
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(x.size() - from));
}

} // namespace

TEST_CASE("DcsDetector: encode matches the known 023 codeword", "[dcs]") {
    // Bits as sent (LSB first): 110 010 000 001 + parity 11000110111.
    const int sent[23] = {1,1,0,0,1,0,0,0,0,0,0,1, 1,1,0,0,0,1,1,0,1,1,1};
    const uint32_t w = DcsDetector::encode(023);
    for (int i = 0; i < 23; ++i) {
        INFO("bit " << i);
        CHECK(int((w >> i) & 1) == sent[i]);
    }
}

TEST_CASE("DcsDetector: inverted aliases pair standard codes", "[dcs]") {
    CHECK(DcsDetector::invertedAlias(023) == 047);
    CHECK(DcsDetector::invertedAlias(047) == 023);
    for (int c : DcsDetector::kCodes) {
        INFO("code " << c);
        const int a = DcsDetector::invertedAlias(c);
        CHECK(a != 0);
        CHECK(DcsDetector::invertedAlias(a) == c);
    }
    CHECK(DcsDetector::codeName(047) == QString("047N = 023I"));
    CHECK(DcsDetector::codeName(0).isEmpty());
}

TEST_CASE("DcsDetector: detects codes under voice and noise", "[dcs]") {
    for (int code : {023, 0132, 0411, 0754}) {
        DcsDetector d;
        d.prepare(kFs);
        auto x = makeAudio(code, false, 0.15, 0.8, 0.05);
        runBlocks(d, x);
        INFO("code " << code);
        CHECK(d.detectedCode() == code);
    }
}

TEST_CASE("DcsDetector: tolerates clock offset and DC", "[dcs]") {
    DcsDetector d;
    d.prepare(kFs);
    auto x = makeAudio(0265, false, 0.2, 0.3, 0.02, 1.5, 1.003, 15.8);
    for (auto& v : x) v += 0.1f;
    runBlocks(d, x);
    CHECK(d.detectedCode() == 0265);
}

TEST_CASE("DcsDetector: inverted transmission reports the normal alias", "[dcs]") {
    DcsDetector d;
    d.prepare(kFs);
    auto x = makeAudio(023, true, 0.15, 0.5, 0.03);
    runBlocks(d, x);
    CHECK(d.detectedCode() == 047);
}

TEST_CASE("DcsDetector: no detection on noise or voice alone", "[dcs]") {
    SECTION("noise") {
        DcsDetector d;
        d.prepare(kFs);
        auto x = makeAudio(0, false, 0.0, 0.0, 0.3, 3.0);
        runBlocks(d, x);
        CHECK(d.detectedCode() == 0);
    }
    SECTION("voice") {
        DcsDetector d;
        d.prepare(kFs);
        auto x = makeAudio(0, false, 0.0, 0.8, 0.01, 3.0);
        runBlocks(d, x);
        CHECK(d.detectedCode() == 0);
    }
}

TEST_CASE("DcsDetector: detection expires after the code stops", "[dcs]") {
    DcsDetector d;
    d.prepare(kFs);
    auto x = makeAudio(0115, false, 0.15, 0.0, 0.02);
    runBlocks(d, x);
    REQUIRE(d.detectedCode() == 0115);
    auto quiet = makeAudio(0, false, 0.0, 0.0, 0.02, 1.0);
    runBlocks(d, quiet);
    CHECK(d.detectedCode() == 0);
}

TEST_CASE("DcsDetector: code squelch", "[dcs]") {
    SECTION("matching code passes audio") {
        DcsDetector d;
        d.prepare(kFs);
        d.setParam("DCS", 0132);
        auto x = makeAudio(0132, false, 0.15, 0.5, 0.0);
        runBlocks(d, x);
        CHECK(rms(x, x.size() * 3 / 4) > 0.2);
    }
    SECTION("inverted alias passes audio") {
        DcsDetector d;
        d.prepare(kFs);
        d.setParam("DCS", 023);
        auto x = makeAudio(023, true, 0.15, 0.5, 0.0);
        runBlocks(d, x);
        CHECK(rms(x, x.size() * 3 / 4) > 0.2);
    }
    SECTION("wrong code mutes") {
        DcsDetector d;
        d.prepare(kFs);
        d.setParam("DCS", 0132);
        auto x = makeAudio(0134, false, 0.15, 0.5, 0.0);
        runBlocks(d, x);
        CHECK(rms(x, x.size() / 4) == 0.0);
    }
    SECTION("off passes everything") {
        DcsDetector d;
        d.prepare(kFs);
        CHECK_FALSE(d.setParam("CTCSS", 88.5));
        auto x = makeAudio(0, false, 0.0, 0.5, 0.0, 0.5);
        runBlocks(d, x);
        CHECK(rms(x, 0) > 0.2);
    }
}
