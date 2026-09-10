#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "NfmModem.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// Pure FM-modulated baseband I/Q (float32, interleaved). Carrier at 0 Hz,
// modulated by a sine at freqHz with peak deviation devHz.
static QVector<float> makeFmSignal(double sr, int numSamples,
                                   double freqHz, double devHz,
                                   double amplitude = 0.9)
{
    QVector<float> iq(numSamples * 2);
    double phase = 0.0;
    for (int n = 0; n < numSamples; ++n) {
        phase += (2.0 * kPi * devHz * std::sin(2.0 * kPi * freqHz * n / sr)) / sr;
        while (phase >  kPi) phase -= 2.0 * kPi;
        while (phase < -kPi) phase += 2.0 * kPi;
        iq[2 * n]     = static_cast<float>(std::cos(phase) * amplitude);
        iq[2 * n + 1] = static_cast<float>(std::sin(phase) * amplitude);
    }
    return iq;
}

// DFT amplitude at a single frequency (peak-normalised: 1.0 = full-scale sine).
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
// NFM-T1 — narrowband FM discriminator recovers a 1 kHz tone.
//   Recovered audio amplitude ≈ dev / maxDeviation (constant-envelope FM), so
//   2.5 kHz deviation into a ±5 kHz modem gives ≈ 0.5 full-scale.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("NFM discriminator recovers 1 kHz tone at correct level", "[nfm][demod]") {
    constexpr double kSR     = 4'000'000.0;
    constexpr double kTone   =     1'000.0;
    constexpr double kDev    =     2'500.0;
    constexpr double kMaxDev =     5'000.0;
    constexpr int    kBlocks = 8;

    NfmModem dem(kSR, 0.0, 12'500.0, kMaxDev);

    const auto iq    = makeFmSignal(kSR, kBlocks * 16384, kTone, kDev);
    const auto audio = runDemod(dem, iq);
    REQUIRE(!audio.isEmpty());

    const int skip = audio.size() / 4;               // FIR warmup
    const QVector<float> steady(audio.constData() + skip, audio.constData() + audio.size());

    const double audioSR  = dem.audioSampleRate();
    const double ampTone  = dftAmplitude(steady, audioSR, kTone);
    const double ampSpur  = dftAmplitude(steady, audioSR, 3'000.0);

    INFO("Audio SR: " << audioSR << "  amp@1kHz: " << ampTone
         << "  amp@3kHz: " << ampSpur);

    CHECK_THAT(ampTone, WithinAbs(kDev / kMaxDev, 0.15));   // ≈ 0.5
    CHECK(ampTone > ampSpur * 3.0);                          // tone dominates
}

// ─────────────────────────────────────────────────────────────────────────────
// NFM-T2 — narrow audio filter: a tone above the ~4 kHz audio passband is cut.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("NFM audio filter passes 1 kHz, rejects 8 kHz", "[nfm][filter]") {
    constexpr double kSR     = 4'000'000.0;
    constexpr double kDev    =     2'000.0;
    constexpr int    kBlocks = 8;

    NfmModem demLow (kSR, 0.0, 12'500.0, 5'000.0);
    NfmModem demHigh(kSR, 0.0, 12'500.0, 5'000.0);

    const auto audioLow  = runDemod(demLow,  makeFmSignal(kSR, kBlocks * 16384, 1'000.0, kDev));
    const auto audioHigh = runDemod(demHigh, makeFmSignal(kSR, kBlocks * 16384, 8'000.0, kDev));

    const int skipL = audioLow.size()  / 4;
    const int skipH = audioHigh.size() / 4;
    const QVector<float> steadyLow (audioLow.constData()  + skipL, audioLow.constData()  + audioLow.size());
    const QVector<float> steadyHigh(audioHigh.constData() + skipH, audioHigh.constData() + audioHigh.size());

    const double audioSR = demLow.audioSampleRate();
    const double ampLow  = dftAmplitude(steadyLow,  audioSR, 1'000.0);
    const double ampHigh = dftAmplitude(steadyHigh, audioSR, 8'000.0);

    INFO("amp@1kHz: " << ampLow << "  amp@8kHz: " << ampHigh);
    CHECK(ampLow > ampHigh * 3.0);   // 8 kHz cut by the ~4 kHz audio FIR2
}

// ─────────────────────────────────────────────────────────────────────────────
// NFM-T3 — full-chain sample rate / count sanity (audio ≈ 50 kHz).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("NFM: audio SR ~50 kHz and sample count matches decimation", "[nfm][chain]") {
    constexpr double kSR     = 4'000'000.0;
    constexpr int    kBlocks = 4;
    constexpr int    kN      = kBlocks * 16384;

    NfmModem dem(kSR, 0.0, 12'500.0, 5'000.0);
    const auto audio = runDemod(dem, makeFmSignal(kSR, kN, 1'000.0, 2'500.0));

    CHECK_THAT(dem.audioSampleRate(), WithinAbs(50'000.0, 5'000.0));

    const int D1       = dem.decimation1();
    const int expected = kN / (D1 * 10);
    INFO("D1=" << D1 << "  expected: " << expected << "  got: " << audio.size());
    CHECK(std::abs(audio.size() - expected) <= expected / 10);
}
