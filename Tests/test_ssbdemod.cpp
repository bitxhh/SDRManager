#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SsbModem.h"

#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// Single complex exponential at freqHz (may be negative). Interleaved float32.
//   +f lands on the upper sideband, -f on the lower sideband.
static QVector<float> makeComplexTone(double sr, int numSamples,
                                      double freqHz, double amplitude = 0.7)
{
    QVector<float> iq(numSamples * 2);
    for (int n = 0; n < numSamples; ++n) {
        const double ph = 2.0 * kPi * freqHz * n / sr;
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

// Demodulate a tone at +audioFreq and at -audioFreq through a fresh modem of the
// given sideband, returning {passAmp, imageAmp} — the recovered level of the
// wanted sideband and of the rejected image, both measured at |audioFreq|.
struct SidebandResult { double pass; double image; };

static SidebandResult measureSideband(int sideband, double audioFreq)
{
    constexpr double kSR     = 4'000'000.0;
    constexpr int    kBlocks = 8;

    // For USB (+1) the wanted tone is at +audioFreq; for LSB (-1) at -audioFreq.
    const double wantedHz = sideband * audioFreq;
    const double imageHz  = -wantedHz;

    SsbModem demWanted(kSR, 0.0, sideband, 2'800.0);
    SsbModem demImage (kSR, 0.0, sideband, 2'800.0);

    const auto aWanted = runDemod(demWanted, makeComplexTone(kSR, kBlocks * 16384, wantedHz));
    const auto aImage  = runDemod(demImage,  makeComplexTone(kSR, kBlocks * 16384, imageHz));

    const int skipW = aWanted.size() / 4;
    const int skipI = aImage.size()  / 4;
    const QVector<float> sW(aWanted.constData() + skipW, aWanted.constData() + aWanted.size());
    const QVector<float> sI(aImage.constData()  + skipI, aImage.constData()  + aImage.size());

    const double audioSR = demWanted.audioSampleRate();
    return { dftAmplitude(sW, audioSR, audioFreq),
             dftAmplitude(sI, audioSR, audioFreq) };
}

// ─────────────────────────────────────────────────────────────────────────────
// SSB-T1 — USB passes the upper sideband and rejects the lower (image).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("USB passes upper sideband, rejects lower", "[ssb][usb]") {
    const auto r = measureSideband(+1, 1'500.0);
    INFO("USB pass=" << r.pass << "  image=" << r.image
         << "  ratio=" << r.pass / std::max(r.image, 1e-9));
    CHECK(r.pass > 0.25);
    CHECK(r.pass > r.image * 4.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// SSB-T2 — LSB passes the lower sideband and rejects the upper (image).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("LSB passes lower sideband, rejects upper", "[ssb][lsb]") {
    const auto r = measureSideband(-1, 1'500.0);
    INFO("LSB pass=" << r.pass << "  image=" << r.image
         << "  ratio=" << r.pass / std::max(r.image, 1e-9));
    CHECK(r.pass > 0.25);
    CHECK(r.pass > r.image * 4.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// SSB-T3 — full-chain sample rate / count sanity (audio ≈ 50 kHz).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("SSB: audio SR ~50 kHz and sample count matches decimation", "[ssb][chain]") {
    constexpr double kSR     = 4'000'000.0;
    constexpr int    kBlocks = 4;
    constexpr int    kN      = kBlocks * 16384;

    SsbModem dem(kSR, 0.0, +1, 2'800.0);
    const auto audio = runDemod(dem, makeComplexTone(kSR, kN, 1'500.0));

    CHECK_THAT(dem.audioSampleRate(), WithinAbs(50'000.0, 5'000.0));

    const int D1       = dem.decimation1();
    const int expected = kN / (D1 * 10);
    INFO("D1=" << D1 << "  expected: " << expected << "  got: " << audio.size());
    CHECK(std::abs(audio.size() - expected) <= expected / 10);
}
