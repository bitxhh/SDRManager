#pragma once

#include "../Core/IPipelineHandler.h"
#include "DspUtils.h"

#include <QByteArray>
#include <QObject>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// ClassifierHandler — IPipelineHandler that serializes every ~100 ms I/Q block
// into a binary frame and emits frameReady() for ClassifierController to send
// to the Python classifier service.
//
// Channelizer: after setChannel(offset, bw) with bw > 0 the handler shifts the
// VFO offset to DC (NCO), low-pass filters to ±bw and decimates by
// D = floor(sr / (2.5·bw)), so the classifier sees only the tuned station at
// a low rate instead of the whole wideband block. Output samples accumulate
// between frames (at most kMaxChannelSamples, oldest dropped). bw <= 0 sends
// the raw block unchanged (wideband mode).
//
// Threading: processBlock() is called on the RxWorker thread.
//            frameReady() must be connected via Qt::QueuedConnection so the
//            ClassifierController can forward data to the QTcpSocket on the
//            main thread safely.
//
// Frame layout, protocol v3 (little-endian). Must match the docstring and
// parser in Python/classifier_service.py.
//   [4B uint32  payload length (everything after these 4 bytes)]
//   --- payload ---
//   [2B uint16  protocol version (kProtocolVersion)]
//   [2B uint16  header length in bytes, incl. these 4 (kHeaderBytes);
//               I/Q starts at this offset — lets readers skip fields added later]
//   [8B uint64  hardware timestamp]
//   [4B int32   sample count N]
//   [8B float64 sample rate Hz (of the I/Q below, i.e. after decimation)]
//   [8B float64 VFO offset Hz from the RX centre (0 = wideband)]         v2
//   [8B float64 channel bandwidth Hz, one-sided (0 = whole band)]        v2
//   [4B int32   demodulator slot, echoed back in the JSON reply]         v3
//   [N*2*4B float32 I/Q pairs, interleaved: I0,Q0,I1,Q1,...]
// ---------------------------------------------------------------------------
class ClassifierHandler : public QObject, public IPipelineHandler {
    Q_OBJECT

public:
    explicit ClassifierHandler(QObject* parent = nullptr, int slot = 0);

    [[nodiscard]] int slot() const { return slot_; }

    // IPipelineHandler
    void processBlock(const float* iq, int count, double sampleRateHz) override;
    void processBlock(const float* iq, int count, double sampleRateHz,
                      const BlockMeta& meta) override;

    // Minimum milliseconds between frames sent to classifier (rate limit).
    void setIntervalMs(int ms);   // default 100 ms; thread-safe

    // Channel to classify: VFO offset from the RX centre and one-sided
    // bandwidth, Hz. bandwidthHz <= 0 → wideband (raw block). Thread-safe.
    void setChannel(double offsetHz, double bandwidthHz);

    static constexpr uint16_t kProtocolVersion   = 3;
    static constexpr uint16_t kHeaderBytes       = 2 + 2 + 8 + 4 + 8 + 8 + 8 + 4;   // payload header
    static constexpr int      kChannelTaps       = 127;
    static constexpr int      kMaxChannelSamples = 16384;   // per frame

    // Builds one complete frame (length prefix + payload). Public for tests.
    static QByteArray serialize(const float* iq, int count,
                                double sampleRateHz, uint64_t timestamp,
                                double vfoOffsetHz = 0.0, double bandwidthHz = 0.0,
                                int32_t slot = 0);

signals:
    // Emitted on RxWorker thread — connect via Qt::QueuedConnection.
    void frameReady(QByteArray frame);

private:
    void rebuildChannelizer(double sampleRateHz);
    void channelize(const float* iq, int count);

    std::atomic<int> intervalMs_{100};

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastEmit_{};

    // Requested channel (any thread) → applied on the RxWorker thread.
    std::atomic<double> offsetHz_{0.0};
    std::atomic<double> bandwidthHz_{0.0};
    std::atomic<bool>   channelDirty_{true};

    // Channelizer state (RxWorker thread only).
    dsp::Nco                 nco_;
    dsp::FirComplexDecimator fir_;
    std::vector<float>       chanBuf_;     // interleaved I/Q at chanSr_
    double inputSr_{0.0};
    double chanSr_{0.0};
    double chanOffset_{0.0};
    double chanBw_{0.0};                   // 0 = passthrough
    const int slot_;
};
