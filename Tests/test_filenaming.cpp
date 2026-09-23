#include <catch2/catch_test_macros.hpp>

#include "FileNaming.h"

#include <QDir>
#include <QRegularExpression>

// Readable QString output in Catch2 failure messages.
template <>
struct Catch::StringMaker<QString> {
    static std::string convert(const QString& s) { return '"' + s.toStdString() + '"'; }
};

static ChannelDescriptor rx(int idx) { return {ChannelDescriptor::RX, idx}; }

// ─────────────────────────────────────────────────────────────────────────────
// Source tags
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("perChannelSource uses the channel index", "[filenaming]") {
    CHECK(FileNaming::perChannelSource(rx(0)) == "rx0");
    CHECK(FileNaming::perChannelSource(rx(1)) == "rx1");
}

TEST_CASE("combinedSource picks a tag by channel count", "[filenaming]") {
    CHECK(FileNaming::combinedSource({}) == "rx0");
    CHECK(FileNaming::combinedSource({rx(1)}) == "rx1");
    CHECK(FileNaming::combinedSource({rx(0), rx(1)}) == "dualrx");
    CHECK(FileNaming::combinedSource({rx(0), rx(1), rx(2)}) == "triplerx");
    CHECK(FileNaming::combinedSource({rx(0), rx(1), rx(2), rx(3)}) == "quadrorx");
    CHECK(FileNaming::combinedSource({rx(0), rx(1), rx(2), rx(3), rx(4)}) == "multirx5");
}

// ─────────────────────────────────────────────────────────────────────────────
// Number formatting
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("formatFrequency prints MHz with three decimals", "[filenaming]") {
    CHECK(FileNaming::formatFrequency(102.0e6)   == "102.000MHz");
    CHECK(FileNaming::formatFrequency(433.9205e6) == "433.921MHz");
    CHECK(FileNaming::formatFrequency(0.0)       == "0.000MHz");
}

TEST_CASE("formatSampleRate switches between MSps and kSps at 1 MHz", "[filenaming]") {
    CHECK(FileNaming::formatSampleRate(4.0e6)    == "4.000MSps");
    CHECK(FileNaming::formatSampleRate(1.0e6)    == "1.000MSps");
    CHECK(FileNaming::formatSampleRate(999.0e3)  == "999.000kSps");
    CHECK(FileNaming::formatSampleRate(48.0e3)   == "48.000kSps");
}

TEST_CASE("currentTimestamp has yyyyMMdd_HHmmss shape", "[filenaming]") {
    static const QRegularExpression re(QStringLiteral("^\\d{8}_\\d{6}$"));
    const QString ts = FileNaming::currentTimestamp();
    CHECK(re.match(ts).hasMatch());
}

// ─────────────────────────────────────────────────────────────────────────────
// compose / composeWithSuffix
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("compose builds the canonical filename", "[filenaming]") {
    const QString ts = QStringLiteral("20260412_153045");

    SECTION("empty dir gives a bare name") {
        CHECK(FileNaming::compose({}, ts, "rx0", 102.0e6, 4.0e6, "cf32")
              == "20260412_153045_rx0_102.000MHz_4.000MSps.cf32");
    }
    SECTION("leading dot in extension is not doubled") {
        CHECK(FileNaming::compose({}, ts, "rx0", 102.0e6, 4.0e6, ".cf32")
              == "20260412_153045_rx0_102.000MHz_4.000MSps.cf32");
    }
    SECTION("non-empty dir is joined via QDir") {
        const QString dir = QStringLiteral("C:/recordings");
        CHECK(FileNaming::compose(dir, ts, "dualrx", 102.0e6, 500.0e3, "cf32")
              == QDir(dir).filePath("20260412_153045_dualrx_102.000MHz_500.000kSps.cf32"));
    }
}

TEST_CASE("composeWithSuffix inserts the suffix after the source tag", "[filenaming]") {
    const QString ts = QStringLiteral("20260412_153045");
    CHECK(FileNaming::composeWithSuffix({}, ts, "dualrx", "fm0", 102.0e6, 48.0e3, "wav")
          == "20260412_153045_dualrx_fm0_102.000MHz_48.000kSps.wav");
    CHECK(FileNaming::composeWithSuffix({}, ts, "dualrx", "bp150kHz", 102.0e6, 500.0e3, ".cf32")
          == "20260412_153045_dualrx_bp150kHz_102.000MHz_500.000kSps.cf32");
}

// ─────────────────────────────────────────────────────────────────────────────
// ChannelDescriptor ordering
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("ChannelDescriptor orders by direction, then index", "[filenaming][channel]") {
    const ChannelDescriptor tx0{ChannelDescriptor::TX, 0};
    CHECK(rx(0) == rx(0));
    CHECK_FALSE(rx(0) == tx0);
    CHECK(rx(0) < rx(1));
    CHECK(rx(5) < tx0);   // RX sorts before TX regardless of index
    CHECK_FALSE(tx0 < rx(5));
}
