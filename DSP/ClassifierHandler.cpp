#include "ClassifierHandler.h"

#include <QSysInfo>
#include <algorithm>
#include <cmath>
#include <cstring>

ClassifierHandler::ClassifierHandler(QObject* parent, int slot)
    : QObject(parent)
    , slot_(slot)
{}

void ClassifierHandler::setIntervalMs(int ms) {
    intervalMs_.store(ms);
}

void ClassifierHandler::setChannel(double offsetHz, double bandwidthHz) {
    offsetHz_.store(offsetHz);
    bandwidthHz_.store(bandwidthHz);
    channelDirty_.store(true);
}

// ---------------------------------------------------------------------------
// Channelizer: NCO → FIR lowpass → ÷D
// ---------------------------------------------------------------------------
void ClassifierHandler::rebuildChannelizer(double sr) {
    inputSr_    = sr;
    chanOffset_ = offsetHz_.load();
    chanBuf_.clear();

    const double bw = bandwidthHz_.load();
    if (bw <= 0.0 || sr <= 0.0) {
        chanBw_ = 0.0;
        chanSr_ = sr;
        return;
    }
    chanBw_ = std::min(bw, 0.45 * sr);
    const int d = std::max(1, static_cast<int>(std::floor(sr / (2.5 * chanBw_))));
    chanSr_ = sr / d;
    const double fc = std::min(chanBw_, 0.45 * chanSr_);
    fir_.setup(dsp::designLowpassFir(kChannelTaps, fc / sr), d);
    nco_.reset();
    nco_.setFrequency(chanOffset_, sr);
}

void ClassifierHandler::channelize(const float* iq, int count) {
    std::complex<double> y;
    for (int i = 0; i < count; ++i) {
        const std::complex<double> s(iq[2 * i], iq[2 * i + 1]);
        if (fir_.process(nco_.mix(s), y)) {
            chanBuf_.push_back(static_cast<float>(y.real()));
            chanBuf_.push_back(static_cast<float>(y.imag()));
        }
    }
    const std::size_t cap = 2 * static_cast<std::size_t>(kMaxChannelSamples);
    if (chanBuf_.size() > cap)
        chanBuf_.erase(chanBuf_.begin(),
                       chanBuf_.begin() + static_cast<std::ptrdiff_t>(chanBuf_.size() - cap));
}

// ---------------------------------------------------------------------------
// IPipelineHandler
// ---------------------------------------------------------------------------
void ClassifierHandler::processBlock(const float* iq, int count, double sampleRateHz) {
    processBlock(iq, count, sampleRateHz, BlockMeta{});
}

void ClassifierHandler::processBlock(const float* iq, int count,
                                     double sampleRateHz, const BlockMeta& meta)
{
    if (channelDirty_.exchange(false) || sampleRateHz != inputSr_)
        rebuildChannelizer(sampleRateHz);
    if (chanBw_ > 0.0)
        channelize(iq, count);

    const auto now = Clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - lastEmit_).count();
    if (elapsedMs < intervalMs_.load()) return;

    if (chanBw_ <= 0.0) {
        lastEmit_ = now;
        emit frameReady(serialize(iq, count, sampleRateHz, meta.timestamp, 0.0, 0.0, slot_));
        return;
    }
    if (chanBuf_.empty()) return;
    lastEmit_ = now;
    emit frameReady(serialize(chanBuf_.data(), static_cast<int>(chanBuf_.size() / 2),
                              chanSr_, meta.timestamp, chanOffset_, chanBw_, slot_));
    chanBuf_.clear();
}

// ---------------------------------------------------------------------------
// Frame serialization — little-endian throughout
// ---------------------------------------------------------------------------
QByteArray ClassifierHandler::serialize(const float* iq, int count,
                                        double sampleRateHz, uint64_t timestamp,
                                        double vfoOffsetHz, double bandwidthHz,
                                        int32_t slot)
{
    const int32_t  n          = static_cast<int32_t>(count);
    const uint32_t payloadLen = static_cast<uint32_t>(kHeaderBytes + n * 2 * sizeof(float));

    QByteArray buf;
    buf.reserve(static_cast<qsizetype>(4 + payloadLen));

    auto appendU16 = [&](uint16_t v) {
        char b[2];
        b[0] = static_cast<char>(v & 0xFF);
        b[1] = static_cast<char>((v >> 8) & 0xFF);
        buf.append(b, 2);
    };
    auto appendU32 = [&](uint32_t v) {
        char b[4];
        b[0] = static_cast<char>(v & 0xFF);
        b[1] = static_cast<char>((v >> 8) & 0xFF);
        b[2] = static_cast<char>((v >> 16) & 0xFF);
        b[3] = static_cast<char>((v >> 24) & 0xFF);
        buf.append(b, 4);
    };
    auto appendU64 = [&](uint64_t v) {
        char b[8];
        for (int i = 0; i < 8; ++i)
            b[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
        buf.append(b, 8);
    };
    auto appendI32 = [&](int32_t v) {
        appendU32(static_cast<uint32_t>(v));
    };
    auto appendF64 = [&](double v) {
        char b[8]; std::memcpy(b, &v, 8);  // native (x86 = LE)
        buf.append(b, 8);
    };

    appendU32(payloadLen);
    appendU16(kProtocolVersion);
    appendU16(kHeaderBytes);
    appendU64(timestamp);
    appendI32(n);
    appendF64(sampleRateHz);
    appendF64(vfoOffsetHz);
    appendF64(bandwidthHz);
    appendI32(slot);
    buf.append(reinterpret_cast<const char*>(iq),
               static_cast<qsizetype>(n * 2 * sizeof(float)));
    return buf;
}
