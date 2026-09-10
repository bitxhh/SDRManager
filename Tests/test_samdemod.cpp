#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SamModem.h"

#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// AM signal: carrier at carrierHz, tone-modulated at modFreqHz with depth m.
//   env = 1 + m·sin(2π·modFreq·t),  iq = env·A·e^{j2π·carrier·t}
// A synchronous detector locked to the carrier recovers env·A; after DC removal
// the audio tone amplitude is ≈ A·m.
static QVector<float> makeAmSignal(double sr, int numSamples,
                                   double modFreqHz, double carrierHz,
                                   double depth = 0.8, double amplitude = 0.5)
{
    QVector<float> iq(numSamples * 2);
    for (int n = 0; n < numSamples; ++n) {
        const double env   = 1.0 + depth * std::sin(2.0 * kPi * modFreqHz * n / sr);
        const double phase = 2.0 * kPi * carrierHz * n / sr;
        iq[2 * n]     = static_cast<float>(env * amplitude * std::cos(phase));
        iq[2 * n + 1] = static_cast<float>(env * amplitude * std::sin(phase));
    }
    return iq;
}

static double dftAmplitude(const QVector<float>& signal, double fs, double freq)
{
    double re = 0.0, im = 0.0;
    const int N = signal.size();
    for (int n = 0; n < N; ++n) {
        re += signal[n] * std::cos(2.0 * kPi * freq * n / fs);
        im -= signal[n] * std::sin(2.0 * kPi * freq * n / fs);
    }
    return 2.0 * std::sqrt(re * re + im * im) / N;
}

static double meanValue(const QVector<float>& signal)
{
    if (signal.isEmpty()) return 0.0;
    double sum = 0.0;
    for (float v : signal) sum += v;
    return sum / signal.size();
}

template <class Modem>
static QVector<float> runDemod(Modem& dem, const QVector<float>& iq, int blockSize = 16384)
{
    QVector<float> out;
    const int total = iq.size() / 2;
    for (int offset = 0; offset < total; offset += blockSize) {
        const int count = std::min(blockSize, total - offset);
        out.append(dem.pushBlock(iq.constData() + offset * 2, count));
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// SAM-T1 — synchronous detector recovers a 1 kHz tone at ≈ A·depth, DC removed.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("SAM recovers 1 kHz tone with carrier locked", "[sam][demod]") {
    constexpr double kSR      = 4'000'000.0;
    constexpr double kCarrier =    50'000.0;
    constexpr double kMod     =     1'000.0;
    constexpr double kDepth   =        0.8;
    constexpr double kAmp     =        0.5;
    constexpr int    kBlocks  = 10;

    SamModem dem(kSR, kCarrier, 5'000.0, 100.0);
    const auto audio = runDemod(dem, makeAmSignal(kSR, kBlocks * 16384, kMod, kCarrier, kDepth, kAmp));
    REQUIRE(!audio.isEmpty());

    const int skip = audio.size() / 2;                 // allow PLL to lock
    const QVector<float> steady(audio.constData() + skip, audio.constData() + audio.size());

    const double audioSR = dem.audioSampleRate();
    const double ampTone = dftAmplitude(steady, audioSR, kMod);
    const double ampSpur = dftAmplitude(steady, audioSR, 3'000.0);

    INFO("Audio SR: " << audioSR << "  amp@1kHz=" << ampTone
         << "  amp@3kHz=" << ampSpur << "  mean=" << meanValue(steady));

    CHECK_THAT(ampTone, WithinAbs(kAmp * kDepth, 0.15));   // ≈ 0.4
    CHECK(ampTone > ampSpur * 3.0);
    CHECK(std::abs(meanValue(steady)) < 0.05);             // carrier DC removed
}

// ─────────────────────────────────────────────────────────────────────────────
// SAM-T2 — the carrier PLL pulls in an offset carrier (within its limit).
//   Carrier sits 300 Hz off the tuned station offset; SAM should still lock
//   and recover the tone.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("SAM pulls in an off-tuned carrier", "[sam][pll]") {
    constexpr double kSR      = 4'000'000.0;
    constexpr double kOffset  =    50'000.0;   // where the modem is tuned
    constexpr double kCarrier =    50'300.0;   // actual carrier, +300 Hz
    constexpr double kMod     =     1'000.0;
    constexpr int    kBlocks  = 12;

    SamModem dem(kSR, kOffset, 5'000.0, 100.0);
    const auto audio = runDemod(dem, makeAmSignal(kSR, kBlocks * 16384, kMod, kCarrier));
    REQUIRE(!audio.isEmpty());

    const int skip = audio.size() / 2;
    const QVector<float> steady(audio.constData() + skip, audio.constData() + audio.size());

    const double audioSR = dem.audioSampleRate();
    const double ampTone = dftAmplitude(steady, audioSR, kMod);
    const double ampSpur = dftAmplitude(steady, audioSR, 3'000.0);

    INFO("amp@1kHz=" << ampTone << "  amp@3kHz=" << ampSpur);
    CHECK(ampTone > 0.15);
    CHECK(ampTone > ampSpur * 3.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// SAM-T3 — full-chain sample rate / count sanity (audio ≈ 50 kHz).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("SAM: audio SR ~50 kHz and sample count matches decimation", "[sam][chain]") {
    constexpr double kSR     = 4'000'000.0;
    constexpr int    kBlocks = 4;
    constexpr int    kN      = kBlocks * 16384;

    SamModem dem(kSR, 50'000.0, 5'000.0, 100.0);
    const auto audio = runDemod(dem, makeAmSignal(kSR, kN, 1'000.0, 50'000.0));

    CHECK_THAT(dem.audioSampleRate(), WithinAbs(50'000.0, 5'000.0));

    const int D1       = dem.decimation1();
    const int expected = kN / (D1 * 10);
    INFO("D1=" << D1 << "  expected: " << expected << "  got: " << audio.size());
    CHECK(std::abs(audio.size() - expected) <= expected / 10);
}
