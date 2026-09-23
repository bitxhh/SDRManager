#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ToneGenerator.h"

#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinAbs;

static constexpr double kPi = 3.14159265358979323846;

// Unwrapped phase increment between consecutive interleaved I/Q samples.
static double measuredPhaseStep(const std::vector<int16_t>& iq, int n)
{
    const double a0 = std::atan2(iq[2 * n + 1], iq[2 * n]);
    const double a1 = std::atan2(iq[2 * n + 3], iq[2 * n + 2]);
    return std::remainder(a1 - a0, 2.0 * kPi);
}

// ─────────────────────────────────────────────────────────────────────────────
// Basic output
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("ToneGenerator at zero offset emits constant I, zero Q", "[tonegen]") {
    ToneGenerator gen(0.0, 0.5f);
    std::vector<int16_t> buf(2 * 256, -1);

    REQUIRE(gen.generateBlock(buf.data(), 256, 1.0e6) == 256);
    const int16_t expectedI = static_cast<int16_t>(0.5f * 32767.0f);
    for (int n = 0; n < 256; ++n) {
        CHECK(buf[2 * n]     == expectedI);
        CHECK(buf[2 * n + 1] == 0);
    }
}

TEST_CASE("ToneGenerator produces a constant-envelope tone of the requested frequency",
          "[tonegen]") {
    const double sr  = 1.0e6;
    const double off = 12.5e3;
    const float  amp = 0.7f;
    ToneGenerator gen(off, amp);

    const int N = 4096;
    std::vector<int16_t> buf(2 * N);
    gen.generateBlock(buf.data(), N, sr);

    const double expectedMag  = amp * 32767.0;
    const double expectedStep = 2.0 * kPi * off / sr;
    for (int n = 0; n < N - 1; ++n) {
        const double mag = std::hypot(buf[2 * n], buf[2 * n + 1]);
        REQUIRE_THAT(mag, WithinAbs(expectedMag, 2.0));   // int16 truncation
        REQUIRE_THAT(measuredPhaseStep(buf, n), WithinAbs(expectedStep, 1e-3));
    }
}

TEST_CASE("ToneGenerator negative offset rotates clockwise", "[tonegen]") {
    ToneGenerator gen(-50.0e3, 0.5f);
    std::vector<int16_t> buf(2 * 64);
    gen.generateBlock(buf.data(), 64, 1.0e6);
    CHECK_THAT(measuredPhaseStep(buf, 10), WithinAbs(-2.0 * kPi * 50.0e3 / 1.0e6, 1e-3));
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase continuity and reset
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("ToneGenerator keeps phase continuous across blocks", "[tonegen]") {
    const double sr = 2.0e6, off = 37.0e3;

    ToneGenerator whole(off, 0.5f);
    std::vector<int16_t> ref(2 * 1000);
    whole.generateBlock(ref.data(), 1000, sr);

    ToneGenerator split(off, 0.5f);
    std::vector<int16_t> parts(2 * 1000);
    split.generateBlock(parts.data(),          333, sr);
    split.generateBlock(parts.data() + 2 * 333, 667, sr);

    // fmod() at the block boundary may shift the last bit of phase.
    for (size_t i = 0; i < ref.size(); ++i)
        REQUIRE(std::abs(ref[i] - parts[i]) <= 1);
}

TEST_CASE("ToneGenerator onTxStarted / onTxStopped reset the phase", "[tonegen]") {
    ToneGenerator gen(100.0e3, 0.5f);
    std::vector<int16_t> first(2 * 16), again(2 * 16), scratch(2 * 123);

    gen.onTxStarted(1.0e6);
    gen.generateBlock(first.data(), 16, 1.0e6);

    gen.generateBlock(scratch.data(), 123, 1.0e6);   // advance phase
    gen.onTxStopped();
    gen.generateBlock(again.data(), 16, 1.0e6);
    CHECK(first == again);

    gen.generateBlock(scratch.data(), 77, 1.0e6);
    gen.onTxStarted(1.0e6);
    gen.generateBlock(again.data(), 16, 1.0e6);
    CHECK(first == again);
}

TEST_CASE("ToneGenerator setters take effect on the next block", "[tonegen]") {
    ToneGenerator gen(0.0, 0.5f);
    std::vector<int16_t> buf(2 * 8);

    gen.setAmplitude(0.25f);
    gen.generateBlock(buf.data(), 8, 1.0e6);
    CHECK(buf[0] == static_cast<int16_t>(0.25f * 32767.0f));

    gen.setToneOffset(250.0e3);   // quarter of the sample rate → 90° per sample
    gen.onTxStarted(1.0e6);
    gen.generateBlock(buf.data(), 8, 1.0e6);
    CHECK_THAT(measuredPhaseStep(buf, 0), WithinAbs(kPi / 2.0, 1e-3));
}
