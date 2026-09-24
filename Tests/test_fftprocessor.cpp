#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "FftProcessor.h"

#include <cmath>
#include <algorithm>

static constexpr double kPi = 3.14159265358979323846;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build an interleaved I/Q buffer for a complex tone at freqHz.
// Signal = exp(j*2π*freqHz*n/sr)  →  I=cos(...), Q=sin(...)
static QVector<float> makeComplexTone(int n, double sr, double freqHz,
                                      double amplitude = 0.9)
{
    QVector<float> iq(n * 2);
    for (int i = 0; i < n; ++i) {
        const double phase = 2.0 * kPi * freqHz * i / sr;
        iq[2 * i]     = static_cast<float>(std::cos(phase) * amplitude);
        iq[2 * i + 1] = static_cast<float>(std::sin(phase) * amplitude);
    }
    return iq;
}

// Returns the index of the bin with maximum power in a FftFrame.
static int peakBin(const FftFrame& frame) {
    return static_cast<int>(
        std::max_element(frame.powerDb.begin(), frame.powerDb.end())
        - frame.powerDb.begin());
}

// ─────────────────────────────────────────────────────────────────────────────
// T8a — DC input: peak must land at the centre bin (0 Hz offset)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("FftProcessor: DC tone peaks at centre bin", "[fft]") {
    // DC: I = constant, Q = 0  →  frequency = 0 Hz = centerFreqMHz
    constexpr int    kN   = 4096;
    constexpr double kSR  = 4'000'000.0;
    constexpr double kCtr = 102.0;

    QVector<float> iq(kN * 2, 0.0f);
    for (int i = 0; i < kN; ++i) {
        iq[2 * i]     = 0.885f;   // I ≈ 0.885 full scale
        iq[2 * i + 1] = 0.0f;    // Q = 0
    }

    const FftFrame frame = FftProcessor::process(iq.constData(), iq.size() / 2, kCtr, kSR);

    REQUIRE(frame.powerDb.size() == kN);
    REQUIRE(frame.freqMHz.size() == kN);

    const int peak = peakBin(frame);
    const int centre = kN / 2;

    INFO("Peak bin: " << peak << "  centre bin: " << centre);
    INFO("Peak freq: " << frame.freqMHz[peak] << " MHz");

    // Peak must be within ±1 bin of centre (DC = centerFreq after FFT-shift)
    CHECK(std::abs(peak - centre) <= 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// T8b — Known-offset tone: peak lands at the expected frequency bin
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("FftProcessor: tone at +500 kHz offset peaks at correct bin", "[fft]") {
    constexpr int    kN      = 4096;
    constexpr double kSR     = 4'000'000.0;
    constexpr double kCtr    = 102.0;
    constexpr double kOffset = 500'000.0;   // +500 kHz from centre

    const auto iq = makeComplexTone(kN, kSR, kOffset);
    const FftFrame frame = FftProcessor::process(iq.constData(), iq.size() / 2, kCtr, kSR);

    const int peak = peakBin(frame);

    // Expected bin: centre + offset / binWidth
    const double binWidthHz = kSR / static_cast<double>(kN);
    const int expected = kN / 2 + static_cast<int>(std::round(kOffset / binWidthHz));

    INFO("Bin width: " << binWidthHz << " Hz");
    INFO("Expected bin: " << expected << "  Peak bin: " << peak);
    INFO("Peak freq: " << frame.freqMHz[peak] << " MHz"
         << "  Expected: " << (kCtr + kOffset / 1e6) << " MHz");

    // Hann window broadens the peak by ~2 bins — allow ±2
    CHECK(std::abs(peak - expected) <= 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// T8c — Frequency axis: first/last bins match SR/2 around centre
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("FftProcessor: frequency axis spans SR/2 around centre", "[fft]") {
    constexpr int    kN   = 2048;
    constexpr double kSR  = 4'000'000.0;
    constexpr double kCtr = 100.0;

    QVector<float> iq(kN * 2, 0.0f);
    const FftFrame frame = FftProcessor::process(iq.constData(), iq.size() / 2, kCtr, kSR);

    const double expectedFirst = kCtr - (kSR / 2.0) / 1e6;   // 98.0 MHz
    const double expectedLast  = kCtr + (kSR / 2.0) / 1e6    // ~102.0 MHz
                                 - (kSR / kN) / 1e6;          // minus one bin

    CHECK_THAT(frame.freqMHz.front(),
               Catch::Matchers::WithinAbs(expectedFirst, 0.001));
    CHECK_THAT(frame.freqMHz.back(),
               Catch::Matchers::WithinAbs(expectedLast, 0.001));
}

// ─────────────────────────────────────────────────────────────────────────────
// T8d — Signal-to-noise: tone clearly above noise floor
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("FftProcessor: signal bin is at least 20 dB above noise floor", "[fft]") {
    constexpr int    kN      = 4096;
    constexpr double kSR     = 4'000'000.0;
    constexpr double kCtr    = 102.0;
    constexpr double kOffset = 300'000.0;

    const auto iq = makeComplexTone(kN, kSR, kOffset, 0.9);
    const FftFrame frame = FftProcessor::process(iq.constData(), iq.size() / 2, kCtr, kSR);

    const int peak = peakBin(frame);
    const double peakDb = frame.powerDb[peak];

    // Average power of all bins except the peak and its neighbours
    double noiseSum = 0.0;
    int    noiseCount = 0;
    for (int i = 0; i < static_cast<int>(frame.powerDb.size()); ++i) {
        if (std::abs(i - peak) > 10) {
            noiseSum += frame.powerDb[i];
            ++noiseCount;
        }
    }
    const double noiseFloorDb = noiseSum / noiseCount;
    const double snrDb = peakDb - noiseFloorDb;

    INFO("Peak: " << peakDb << " dB  Noise floor: " << noiseFloorDb << " dB  SNR: " << snrDb << " dB");
    CHECK(snrDb >= 20.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Metadata: binHz / ENBW and band power
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("FftProcessor: frame carries bin width and Hann ENBW", "[fft]") {
    constexpr int    kN  = 4096;
    constexpr double kSR = 2'000'000.0;

    const QVector<float> iq = makeComplexTone(kN, kSR, 100'000.0);
    const FftFrame frame = FftProcessor::process(iq.constData(), kN, 100.0, kSR);

    CHECK_THAT(frame.binHz, Catch::Matchers::WithinRel(kSR / kN, 1e-12));
    CHECK_THAT(frame.enbwBins, Catch::Matchers::WithinAbs(1.5, 0.01));
    CHECK_THAT(frame.enbwHz(), Catch::Matchers::WithinRel(frame.enbwBins * kSR / kN, 1e-12));
}

TEST_CASE("FftProcessor: band power of a tone matches its amplitude", "[fft]") {
    constexpr int    kN  = 4096;
    constexpr double kSR = 2'000'000.0;
    constexpr double kA  = 0.5;                       // power = 20·log10(0.5) ≈ -6.02 dBFS
    const double expectedDb = 20.0 * std::log10(kA);

    // Half-bin offset: worst-case scalloping for the peak bin (≈ -1.4 dB on
    // Hann), which band integration must recover.
    const double binHz = kSR / kN;
    for (double freqHz : {250.0 * binHz, 250.5 * binHz, -123.25 * binHz}) {
        const QVector<float> iq = makeComplexTone(kN, kSR, freqHz, kA);
        const FftFrame frame = FftProcessor::process(iq.constData(), kN, 100.0, kSR);

        const int peak = peakBin(frame);
        const double bandDb = FftProcessor::bandPowerDb(frame, peak - 8, peak + 8);
        INFO("freq " << freqHz << " Hz, peak bin " << frame.powerDb[peak]
             << " dB, band " << bandDb << " dB");
        CHECK_THAT(bandDb, Catch::Matchers::WithinAbs(expectedDb, 0.5));
    }
}

TEST_CASE("FftProcessor: band power of white noise matches its variance", "[fft]") {
    constexpr int    kN     = 8192;
    constexpr double kSR    = 1'000'000.0;
    constexpr double kSigma = 0.1;                    // per-component std dev
    const double expectedDb = 10.0 * std::log10(2.0 * kSigma * kSigma);   // I² + Q²

    // Deterministic LCG + Box–Muller — no <random> distribution variance across libs.
    uint32_t state = 12345u;
    auto uniform = [&] { state = state * 1664525u + 1013904223u;
                         return (static_cast<double>(state) + 1.0) / 4294967297.0; };
    QVector<float> iq(kN * 2);
    for (int i = 0; i < kN; ++i) {
        const double r = kSigma * std::sqrt(-2.0 * std::log(uniform()));
        const double t = 2.0 * kPi * uniform();
        iq[2 * i]     = static_cast<float>(r * std::cos(t));
        iq[2 * i + 1] = static_cast<float>(r * std::sin(t));
    }
    const FftFrame frame = FftProcessor::process(iq.constData(), kN, 100.0, kSR);

    const double totalDb = FftProcessor::bandPowerDb(frame, 0, kN - 1);
    CHECK_THAT(totalDb, Catch::Matchers::WithinAbs(expectedDb, 0.5));

    // Half the band carries half the power.
    const double halfDb = FftProcessor::bandPowerDb(frame, 0, kN / 2 - 1);
    CHECK_THAT(halfDb, Catch::Matchers::WithinAbs(expectedDb - 3.01, 0.5));
}

TEST_CASE("FftProcessor: noise floor ignores narrowband carriers", "[fft]") {
    QVector<double> p(100, -100.0);
    for (int i = 0; i < 100; ++i) p[i] = -100.0 + 0.01 * i;   // -100 … -99.01 dB
    for (int i = 10; i < 30; ++i) p[i] = -20.0;               // 20 % occupied by signals

    // 20th percentile of 100 bins lands on a noise bin, not a carrier.
    const double floor = FftProcessor::noiseFloorDb(p, 0, 99);
    CHECK(floor < -99.0);

    // Median of pure noise sub-range.
    CHECK_THAT(FftProcessor::noiseFloorDb(p, 40, 60, 0.5),
               Catch::Matchers::WithinAbs(-100.0 + 0.01 * 50, 1e-9));

    // Range is clamped to the vector; empty range gives NaN.
    CHECK_THAT(FftProcessor::noiseFloorDb(p, -5, 200, 0.0),
               Catch::Matchers::WithinAbs(-100.0, 1e-9));
    CHECK(std::isnan(FftProcessor::noiseFloorDb(p, 50, 40)));
    CHECK(std::isnan(FftProcessor::noiseFloorDb(QVector<double>{}, 0, 10)));
}
