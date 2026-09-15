#pragma once

#include <QObject>
#include <QVector>
#include <QAudioSink>
#include <QMediaDevices>
#include <QIODevice>
#include <QTimer>
#include <cstdint>

// ---------------------------------------------------------------------------
// FmAudioOutput — receives float32 mono audio from FmModem, resamples
// to the device's preferred format and writes to QAudioSink (WASAPI).
//
// Format strategy:
//   1. Query QAudioDevice::preferredFormat() — this is what WASAPI actually
//      wants (usually 48 kHz stereo Float32 or Int16 on modern Windows).
//   2. If the preferred format is not supported for some reason, fall back to
//      48 kHz stereo Int16 (lowest-common-denominator Windows format).
//   The resampler converts from the demodulator SR to whatever rate the device
//   prefers, so no assumptions are made about the output rate.
//
// Usage:
//   auto* audio = new FmAudioOutput(this);
//   connect(worker, &RxWorker::audioReady,
//           audio,  &FmAudioOutput::push, Qt::QueuedConnection);
//   audio->setVolume(0.8f);
// ---------------------------------------------------------------------------
class FmAudioOutput : public QObject {
    Q_OBJECT

public:
    explicit FmAudioOutput(QObject* parent = nullptr);
    ~FmAudioOutput() override;

    void  setVolume(float v);
    float volume() const { return volume_; }

    void teardown();

    [[nodiscard]] bool    isRunning()   const;
    [[nodiscard]] QString statusText()  const { return statusText_; }

public slots:
    void push(QVector<float> samples, double sampleRateHz);

signals:
    void statusChanged(const QString& message, bool isError);

private:
    // ── Linear resampler ─────────────────────────────────────────────────────
    struct LinearResampler {
        double phase{0.0};
        float  prev{0.0f};
        QVector<float> process(const QVector<float>& in, double inRate, double outRate);
        void reset() { phase = 0.0; prev = 0.0f; }
    } resampler_;

    // ── Sink state ────────────────────────────────────────────────────────────
    QAudioSink* sink_{nullptr};
    QIODevice*  device_{nullptr};
    double      openedForRate_{0.0};
    int         outRate_{48'000};    // actual output sample rate
    bool        outIsFloat_{true};   // true = Float32, false = Int16

    // ── Watchdog timer ────────────────────────────────────────────────────────
    // Logs sink state every 2 s so silent failures are visible in sdrmanager.log.
    QTimer*     watchdog_{nullptr};

    // ── Settings / state ──────────────────────────────────────────────────────
    float   volume_{0.8f};
    QString statusText_;
    int     diagCount_{0};

    // ── AGC (RMS-based automatic gain control) ────────────────────────────────
    // Adapts gain so output RMS ≈ kAgcTarget regardless of signal strength.
    // Prevents clipping on strong stations; boosts weak/quiet audio.
    float agcGain_{1.0f};
    static constexpr float kAgcTarget  = 0.12f;   // target RMS
    static constexpr float kAgcAttack  = 0.10f;   // fast decay on loud signal
    static constexpr float kAgcRelease = 0.002f;  // slow rise on quiet signal
    static constexpr float kAgcMin     = 0.1f;    // floor — allows reducing gain when input is already loud
    static constexpr float kAgcMax     = 50.0f;   // ceiling — don't amplify pure noise

    // ── Latency control ───────────────────────────────────────────────────────
    // push() runs on the main thread: a UI stall starves the sink, then the
    // queued blocks arrive in a burst and pin the 300 ms buffer full — every
    // later hiccup drops a whole block (choppy audio that never recovers).
    //   • fill > kHighWaterMs → drop blocks until fill ≤ kTargetMs (one clean
    //     skip instead of endless partial writes);
    //   • never write more than bytesFree() (whole frames only);
    //   • slow resample-ratio trim (±kMaxRateTrim) pulls the smoothed fill
    //     toward kTargetMs, absorbing SDR-crystal vs sound-card clock drift.
    static constexpr int    kTargetMs    = 100;
    static constexpr int    kHighWaterMs = 200;
    static constexpr double kMaxRateTrim = 0.002;   // 0.2 % ≈ 3.5 cents
    static constexpr double kFillAvgAlpha = 0.02;   // EMA per block (~0.4 s)

    bool    draining_{false};
    double  fillMsAvg_{-1.0};      // < 0 — not initialised
    qint64  droppedBytes_{0};      // since last watchdog report
    qint64  truncatedBytes_{0};    // since last watchdog report

    [[nodiscard]] int frameBytes() const { return 2 * (outIsFloat_ ? 4 : 2); }
    void writePcm(const char* data, qint64 bytes);

    bool openSink(double sampleRateHz);
};
