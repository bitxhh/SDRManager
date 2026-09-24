#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SpectrumTraces.h"

#include <cmath>

using Catch::Matchers::WithinAbs;

static QVector<double> axis(int n, double startMHz) {
    QVector<double> f(n);
    for (int i = 0; i < n; ++i) f[i] = startMHz + 0.001 * i;
    return f;
}

TEST_CASE("SpectrumTraces: first frame seeds every trace", "[traces]") {
    SpectrumTraces t;
    const auto f = axis(3, 100.0);
    t.update(f, {-50.0, -60.0, -70.0});
    REQUIRE(t.frameCount() == 1);
    for (int k = 0; k < SpectrumTraces::kKindCount; ++k) {
        const auto& tr = t.trace(static_cast<SpectrumTraces::Kind>(k));
        REQUIRE(tr.size() == 3);
        CHECK_THAT(tr[0], WithinAbs(-50.0, 1e-9));
        CHECK_THAT(tr[2], WithinAbs(-70.0, 1e-9));
    }
}

TEST_CASE("SpectrumTraces: max and min hold", "[traces]") {
    SpectrumTraces t;
    const auto f = axis(2, 100.0);
    t.update(f, {-50.0, -50.0});
    t.update(f, {-40.0, -60.0});
    t.update(f, {-45.0, -55.0});
    CHECK(t.trace(SpectrumTraces::Live)[0]    == -45.0);
    CHECK(t.trace(SpectrumTraces::MaxHold)[0] == -40.0);
    CHECK(t.trace(SpectrumTraces::MaxHold)[1] == -50.0);
    CHECK(t.trace(SpectrumTraces::MinHold)[0] == -50.0);
    CHECK(t.trace(SpectrumTraces::MinHold)[1] == -60.0);
}

TEST_CASE("SpectrumTraces: average is linear-power mean during warm-up", "[traces]") {
    SpectrumTraces t;
    t.setAverageAlpha(0.01);                         // warm-up covers both frames
    const auto f = axis(1, 100.0);
    t.update(f, {0.0});                              // 1.0 linear
    t.update(f, {10.0});                             // 10.0 linear
    // mean = 5.5 → 7.404 dB (a dB-average would give 5.0)
    CHECK_THAT(t.trace(SpectrumTraces::Average)[0], WithinAbs(10.0 * std::log10(5.5), 1e-9));
}

TEST_CASE("SpectrumTraces: exponential average converges with alpha", "[traces]") {
    SpectrumTraces t;
    t.setAverageAlpha(0.5);
    const auto f = axis(1, 100.0);
    t.update(f, {0.0});                              // lin = 1
    t.update(f, {10.0});                             // lin = 1 + 0.5·9 = 5.5
    t.update(f, {10.0});                             // lin = 5.5 + 0.5·4.5 = 7.75
    CHECK_THAT(t.trace(SpectrumTraces::Average)[0], WithinAbs(10.0 * std::log10(7.75), 1e-9));
    for (int i = 0; i < 60; ++i) t.update(f, {10.0});
    CHECK_THAT(t.trace(SpectrumTraces::Average)[0], WithinAbs(10.0, 1e-6));
}

TEST_CASE("SpectrumTraces: axis change resets accumulation", "[traces]") {
    SpectrumTraces t;
    t.update(axis(2, 100.0), {-10.0, -10.0});
    t.update(axis(2, 100.0), {-50.0, -50.0});
    REQUIRE(t.frameCount() == 2);
    REQUIRE(t.trace(SpectrumTraces::MaxHold)[0] == -10.0);

    SECTION("center frequency shift") {
        t.update(axis(2, 101.0), {-50.0, -50.0});
    }
    SECTION("bin count change") {
        t.update(axis(3, 100.0), {-50.0, -50.0, -50.0});
    }
    SECTION("span change (sample rate)") {
        t.update({100.0, 100.002}, {-50.0, -50.0});
    }
    CHECK(t.frameCount() == 1);
    CHECK(t.trace(SpectrumTraces::MaxHold)[0] == -50.0);
}

TEST_CASE("SpectrumTraces: clear restarts accumulation on the same axis", "[traces]") {
    SpectrumTraces t;
    const auto f = axis(1, 100.0);
    t.update(f, {-10.0});
    t.update(f, {-50.0});
    t.clear();
    t.update(f, {-30.0});
    CHECK(t.frameCount() == 1);
    CHECK(t.trace(SpectrumTraces::MaxHold)[0] == -30.0);
    CHECK(t.trace(SpectrumTraces::MinHold)[0] == -30.0);
    CHECK_THAT(t.trace(SpectrumTraces::Average)[0], WithinAbs(-30.0, 1e-9));
}

TEST_CASE("SpectrumPeaks: peak, next peak and nearest bin", "[spectrum]") {
    //                      0     1    2     3    4     5     6    7     8
    const QVector<double> p{-90, -40, -90, -70, -60, -60, -95, -50, -80};

    CHECK(SpectrumPeaks::peakIndex(p, 0, 8) == 1);
    CHECK(SpectrumPeaks::peakIndex(p, 2, 8) == 7);
    CHECK(SpectrumPeaks::peakIndex(p, -5, 100) == 1);   // обрезка
    CHECK(SpectrumPeaks::peakIndex(p, 5, 4) == -1);

    // Цепочка Next: -40 → -50 → -60 (плато 4–5, левый край) → нет.
    CHECK(SpectrumPeaks::nextPeakIndex(p, 0, 8, -40.0) == 7);
    CHECK(SpectrumPeaks::nextPeakIndex(p, 0, 8, -50.0) == 4);
    // Ниже -60 локальных максимумов нет: бины 3 (-70) и 8 (-80) — склоны,
    // бин 0 (-90) — край ниже соседа.
    CHECK(SpectrumPeaks::nextPeakIndex(p, 0, 8, -60.0) == -1);
    // Край диапазона выше соседа считается пиком.
    CHECK(SpectrumPeaks::nextPeakIndex(p, 3, 8, -55.0) == 4);
    CHECK(SpectrumPeaks::nextPeakIndex(p, 8, 8, 0.0) == -1);

    const QVector<double> f{100.0, 100.1, 100.2, 100.3};
    CHECK(SpectrumPeaks::nearestBin(f, 99.0)   == 0);
    CHECK(SpectrumPeaks::nearestBin(f, 100.14) == 1);
    CHECK(SpectrumPeaks::nearestBin(f, 100.16) == 2);
    CHECK(SpectrumPeaks::nearestBin(f, 200.0)  == 3);
    CHECK(SpectrumPeaks::nearestBin({}, 1.0)   == -1);
}
