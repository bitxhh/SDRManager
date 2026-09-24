#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AudioProcessor.h"
#include "NfmModem.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// Test stage: multiplies by "gain", counts prepare/reset calls.
struct GainStage : IAudioProcessor {
    double gain     = 1.0;
    double preparedSR = 0.0;
    int*   resets   = nullptr;

    void prepare(double sr) override { preparedSR = sr; }
    void process(float* s, int n) override {
        for (int i = 0; i < n; ++i) s[i] = static_cast<float>(s[i] * gain);
    }
    void reset() override { if (resets) ++*resets; }
    bool setParam(const QString& name, double value) override {
        if (name != QLatin1String("gain")) return false;
        gain = value;
        return true;
    }
};

static QVector<float> makeFmSignal(double sr, int numSamples, double freqHz, double devHz)
{
    QVector<float> iq(numSamples * 2);
    double phase = 0.0;
    for (int n = 0; n < numSamples; ++n) {
        phase += (2.0 * kPi * devHz * std::sin(2.0 * kPi * freqHz * n / sr)) / sr;
        phase = std::remainder(phase, 2.0 * kPi);
        iq[2 * n]     = static_cast<float>(std::cos(phase) * 0.9);
        iq[2 * n + 1] = static_cast<float>(std::sin(phase) * 0.9);
    }
    return iq;
}

static QVector<float> run(ChannelModem& dem, const QVector<float>& iq, int block = 16384)
{
    QVector<float> out;
    const int total = iq.size() / 2;
    for (int off = 0; off < total; off += block)
        out.append(dem.pushBlock(iq.constData() + off * 2, std::min(block, total - off)));
    return out;
}

TEST_CASE("Audio chain: prepare gets audio SR, setAudioParam routes by name", "[audiochain]") {
    NfmModem dem(2'000'000.0, 0.0);
    REQUIRE(dem.audioProcessorCount() == 0);
    CHECK_FALSE(dem.setAudioParam("gain", 2.0));   // empty chain claims nothing

    auto stage = std::make_unique<GainStage>();
    auto* raw  = stage.get();
    dem.addAudioProcessor(std::move(stage));
    REQUIRE(dem.audioProcessorCount() == 1);
    CHECK(raw->preparedSR == dem.audioSampleRate());

    CHECK(dem.setAudioParam("gain", 3.0));
    CHECK(raw->gain == 3.0);
    CHECK_FALSE(dem.setAudioParam("bandwidth", 5000.0));
}

TEST_CASE("Audio chain: runs on pushBlock output after D2", "[audiochain]") {
    constexpr double kSR = 2'000'000.0;
    const auto iq = makeFmSignal(kSR, 400'000, 1000.0, 2500.0);

    NfmModem plain(kSR, 0.0);
    NfmModem gained(kSR, 0.0);
    gained.addAudioProcessor(std::make_unique<GainStage>());
    REQUIRE(gained.setAudioParam("gain", 2.0));

    const auto a = run(plain, iq);
    const auto b = run(gained, iq);
    REQUIRE(a.size() == b.size());
    REQUIRE(!a.isEmpty());
    for (int i = 0; i < a.size(); ++i)
        REQUIRE_THAT(b[i], WithinAbs(2.0 * a[i], 1e-5));

    gained.setAudioParam("gain", 0.0);
    const auto z = run(gained, iq);
    CHECK(std::all_of(z.begin(), z.end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("Audio chain: setOffset resets every stage", "[audiochain]") {
    NfmModem dem(2'000'000.0, 0.0);
    int resets = 0;
    for (int i = 0; i < 2; ++i) {
        auto s = std::make_unique<GainStage>();
        s->resets = &resets;
        dem.addAudioProcessor(std::move(s));
    }
    dem.setOffset(10'000.0);
    CHECK(resets == 2);
}
