#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CwModem.h"

#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// An unmodulated CW carrier: a steady complex tone at carrierHz (a "key-down"
// dash). Placed at a non-zero IF offset so it survives the input DC blocker;
// the modem's NCO (stationOffset) tunes it back to DC before the BFO.
static QVector<float> makeCarrier(double sr, int numSamples,
                                  double carrierHz, double amplitude = 0.7)
{
    QVector<float> iq(numSamples * 2);
    for (int n = 0; n < numSamples; ++n) {
        const double ph = 2.0 * kPi * carrierHz * n / sr;
        iq[2 * n]     = static_cast<float>(std::cos(ph) * amplitude);
        iq[2 * n + 1] = static_cast<float>(std::sin(ph) * amplitude);
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
// CW-T1 — an on-frequency carrier becomes an audible beat at the BFO pitch.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("CW carrier produces a clean beat at the BFO pitch", "[cw][demod]") {
    constexpr double kSR      = 4'000'000.0;
    constexpr double kCarrier =    50'000.0;   // IF offset of the CW signal
    constexpr double kPitch   =       700.0;
    constexpr int    kBlocks  = 8;

    CwModem dem(kSR, kCarrier, 500.0, kPitch);
    const auto audio = runDemod(dem, makeCarrier(kSR, kBlocks * 16384, kCarrier));
    REQUIRE(!audio.isEmpty());

    const int skip = audio.size() / 4;
    const QVector<float> steady(audio.constData() + skip, audio.constData() + audio.size());

    const double audioSR   = dem.audioSampleRate();
    const double ampPitch  = dftAmplitude(steady, audioSR, kPitch);
    const double ampOther  = dftAmplitude(steady, audioSR, 1'500.0);

    INFO("Audio SR: " << audioSR << "  amp@700=" << ampPitch
         << "  amp@1500=" << ampOther);

    CHECK(ampPitch > 0.3);              // strong sidetone
    CHECK(ampPitch > ampOther * 3.0);   // energy concentrated at the pitch
}

// ─────────────────────────────────────────────────────────────────────────────
// CW-T2 — the pitch parameter sets the beat frequency.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("CW pitch parameter moves the beat frequency", "[cw][pitch]") {
    constexpr double kSR      = 4'000'000.0;
    constexpr double kCarrier =    50'000.0;
    constexpr int    kBlocks  = 8;

    CwModem dem(kSR, kCarrier, 500.0, /*pitch*/ 900.0);
    const auto audio = runDemod(dem, makeCarrier(kSR, kBlocks * 16384, kCarrier));

    const int skip = audio.size() / 4;
    const QVector<float> steady(audio.constData() + skip, audio.constData() + audio.size());

    const double audioSR = dem.audioSampleRate();
    const double amp900  = dftAmplitude(steady, audioSR, 900.0);
    const double amp700  = dftAmplitude(steady, audioSR, 700.0);

    INFO("amp@900=" << amp900 << "  amp@700=" << amp700);
    CHECK(amp900 > amp700 * 3.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// CW-T3 — full-chain sample rate / count sanity (audio ≈ 50 kHz).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("CW: audio SR ~50 kHz and sample count matches decimation", "[cw][chain]") {
    constexpr double kSR     = 4'000'000.0;
    constexpr int    kBlocks = 4;
    constexpr int    kN      = kBlocks * 16384;

    CwModem dem(kSR, 50'000.0, 500.0, 700.0);
    const auto audio = runDemod(dem, makeCarrier(kSR, kN, 50'000.0));

    CHECK_THAT(dem.audioSampleRate(), WithinAbs(50'000.0, 5'000.0));

    const int D1       = dem.decimation1();
    const int expected = kN / (D1 * 10);
    INFO("D1=" << D1 << "  expected: " << expected << "  got: " << audio.size());
    CHECK(std::abs(audio.size() - expected) <= expected / 10);
}
