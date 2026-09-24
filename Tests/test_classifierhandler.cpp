#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ClassifierHandler.h"

#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

using Catch::Matchers::WithinAbs;

// Reads a little-endian value of type T at byte offset `off`.
template <typename T>
static T readLE(const QByteArray& b, int off) {
    T v{};
    std::memcpy(&v, b.constData() + off, sizeof(T));
    return v;
}

TEST_CASE("ClassifierHandler::serialize builds a protocol v3 frame", "[classifier]") {
    const int count = 5;
    std::vector<float> iq(count * 2);
    for (std::size_t i = 0; i < iq.size(); ++i) iq[i] = 0.25f * static_cast<float>(i) - 1.0f;

    const double   rate = 2'400'000.0;
    const uint64_t ts   = 0x0123456789ABCDEFULL;
    const QByteArray f  = ClassifierHandler::serialize(iq.data(), count, rate, ts,
                                                       -125'000.0, 12'500.0, 3);

    const int hdr = ClassifierHandler::kHeaderBytes;
    REQUIRE(ClassifierHandler::kProtocolVersion == 3);
    REQUIRE(hdr == 44);
    REQUIRE(f.size() == 4 + hdr + count * 2 * 4);

    CHECK(readLE<uint32_t>(f, 0) == static_cast<uint32_t>(f.size() - 4));
    CHECK(readLE<uint16_t>(f, 4) == ClassifierHandler::kProtocolVersion);
    CHECK(readLE<uint16_t>(f, 6) == hdr);
    CHECK(readLE<uint64_t>(f, 8) == ts);
    CHECK(readLE<int32_t>(f, 16) == count);
    CHECK(readLE<double>(f, 20) == rate);
    CHECK(readLE<double>(f, 28) == -125'000.0);
    CHECK(readLE<double>(f, 36) == 12'500.0);
    CHECK(readLE<int32_t>(f, 44) == 3);

    for (int i = 0; i < count * 2; ++i)
        CHECK(readLE<float>(f, 4 + hdr + i * 4) == iq[static_cast<std::size_t>(i)]);
}

TEST_CASE("ClassifierHandler::serialize with zero samples is header-only", "[classifier]") {
    const QByteArray f = ClassifierHandler::serialize(nullptr, 0, 1e6, 42);
    REQUIRE(f.size() == 4 + ClassifierHandler::kHeaderBytes);
    CHECK(readLE<uint32_t>(f, 0) == ClassifierHandler::kHeaderBytes);
    CHECK(readLE<int32_t>(f, 16) == 0);
}

// Feeds `blocks` blocks of 20000 samples of a complex tone (amplitude 0.5)
// and returns every frame emitted.
static std::vector<QByteArray> runTone(ClassifierHandler& h, double sr, double toneHz,
                                       int blocks) {
    std::vector<QByteArray> frames;
    QObject::connect(&h, &ClassifierHandler::frameReady,
                     [&frames](const QByteArray& f) { frames.push_back(f); });
    h.setIntervalMs(0);
    const int n = 20'000;
    std::vector<float> iq(2 * n);
    long long k = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < n; ++i, ++k) {
            const double ph = 2.0 * dsp::kPi * toneHz * static_cast<double>(k) / sr;
            iq[2 * i]     = static_cast<float>(0.5 * std::cos(ph));
            iq[2 * i + 1] = static_cast<float>(0.5 * std::sin(ph));
        }
        h.processBlock(iq.data(), n, sr, BlockMeta{});
    }
    return frames;
}

static std::vector<std::complex<float>> frameIq(const QByteArray& f) {
    const int hdr = readLE<uint16_t>(f, 6);
    const int n   = readLE<int32_t>(f, 16);
    std::vector<std::complex<float>> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = {readLE<float>(f, 4 + hdr + 8 * i), readLE<float>(f, 4 + hdr + 8 * i + 4)};
    return v;
}

TEST_CASE("ClassifierHandler channelizer shifts the VFO to DC and decimates", "[classifier]") {
    const double sr = 2e6, off = 300'000.0, bw = 12'500.0;
    ClassifierHandler h;
    h.setChannel(off, bw);
    const auto frames = runTone(h, sr, off + 1'000.0, 3);
    REQUIRE(frames.size() == 3);

    const int d = static_cast<int>(std::floor(sr / (2.5 * bw)));   // 64
    REQUIRE(d == 64);
    const QByteArray& f = frames.back();
    CHECK(readLE<double>(f, 20) == sr / d);
    CHECK(readLE<double>(f, 28) == off);
    CHECK(readLE<double>(f, 36) == bw);
    const auto v = frameIq(f);
    REQUIRE(static_cast<int>(v.size()) == 20'000 / d);

    double mag = 0.0, dph = 0.0;
    for (std::size_t i = 1; i < v.size(); ++i) {
        mag += std::abs(v[i]);
        dph += std::arg(v[i] * std::conj(v[i - 1]));
    }
    mag /= static_cast<double>(v.size() - 1);
    dph /= static_cast<double>(v.size() - 1);
    CHECK_THAT(mag, WithinAbs(0.5, 0.02));
    CHECK_THAT(dph * (sr / d) / (2.0 * dsp::kPi), WithinAbs(1'000.0, 5.0));
}

TEST_CASE("ClassifierHandler channelizer rejects out-of-channel signals", "[classifier]") {
    ClassifierHandler h;
    h.setChannel(300'000.0, 12'500.0);
    const auto frames = runTone(h, 2e6, 0.0, 3);
    REQUIRE_FALSE(frames.empty());
    float peak = 0.0f;
    for (const auto& s : frameIq(frames.back())) peak = std::max(peak, std::abs(s));
    CHECK(peak < 5e-4f);
}

TEST_CASE("ClassifierHandler without a channel sends the raw block", "[classifier]") {
    ClassifierHandler h;
    const auto frames = runTone(h, 1e6, 10'000.0, 1);
    REQUIRE(frames.size() == 1);
    const QByteArray& f = frames[0];
    CHECK(readLE<int32_t>(f, 16) == 20'000);
    CHECK(readLE<double>(f, 20) == 1e6);
    CHECK(readLE<double>(f, 28) == 0.0);
    CHECK(readLE<double>(f, 36) == 0.0);
}
