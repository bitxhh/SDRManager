#include "FmAudioOutput.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------
FmAudioOutput::FmAudioOutput(QObject* parent)
    : QObject(parent)
{
    watchdog_ = new QTimer(this);
    watchdog_->setInterval(2000);
    connect(watchdog_, &QTimer::timeout, this, [this]() {
        if (!sink_) return;
        const auto state = sink_->state();
        const auto err   = sink_->error();
        LOG_DEBUG("FmAudioOutput watchdog: state="
                  + std::to_string(static_cast<int>(state))
                  + " error=" + std::to_string(static_cast<int>(err))
                  + " bytesFree=" + std::to_string(sink_->bytesFree())
                  + " fillMsAvg=" + std::to_string(static_cast<int>(fillMsAvg_))
                  + " outRate=" + std::to_string(outRate_)
                  + " isFloat=" + std::to_string(outIsFloat_));
        if (droppedBytes_ > 0 || truncatedBytes_ > 0) {
            const qint64 bytesPerMs = std::max<qint64>(1, outRate_ * frameBytes() / 1000);
            LOG_WARN("FmAudioOutput: latency control dropped "
                     + std::to_string(droppedBytes_ / bytesPerMs) + " ms, truncated "
                     + std::to_string(truncatedBytes_ / bytesPerMs) + " ms in last 2 s");
            droppedBytes_   = 0;
            truncatedBytes_ = 0;
        }
        // Auto-recover from underrun
        if (state == QAudio::StoppedState && err == QAudio::UnderrunError) {
            LOG_WARN("FmAudioOutput: underrun, restarting");
            device_ = sink_->start();
        }
    });
}

FmAudioOutput::~FmAudioOutput() {
    teardown();
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void FmAudioOutput::setVolume(float v) {
    volume_ = std::clamp(v, 0.0f, 1.0f);
    if (sink_) sink_->setVolume(volume_);
}

bool FmAudioOutput::isRunning() const {
    return sink_ && (sink_->state() == QAudio::ActiveState
                  || sink_->state() == QAudio::IdleState);
}

void FmAudioOutput::teardown() {
    if (watchdog_) watchdog_->stop();
    if (sink_) {
        sink_->stop();
        delete sink_;
        sink_   = nullptr;
        device_ = nullptr;
    }
    openedForRate_ = 0.0;
    outRate_       = 48'000;
    outIsFloat_    = true;
    diagCount_     = 0;
    agcGain_       = 1.0f;
    draining_       = false;
    fillMsAvg_      = -1.0;
    droppedBytes_   = 0;
    truncatedBytes_ = 0;
    resampler_.reset();
    statusText_.clear();
    emit statusChanged(QString(), false);
    LOG_DEBUG("FmAudioOutput: torn down");
}

// ---------------------------------------------------------------------------
// push() — called from main thread via QueuedConnection
// ---------------------------------------------------------------------------
void FmAudioOutput::push(QVector<float> samples, double sampleRateHz) {
    if (samples.isEmpty()) return;

    if (!sink_ || openedForRate_ != sampleRateHz) {
        teardown();
        if (!openSink(sampleRateHz)) return;
        openedForRate_ = sampleRateHz;
        watchdog_->start();
    }

    if (!device_) return;

    // Smoothed sink fill level → small resample-ratio trim toward kTargetMs.
    const double bytesPerMs = outRate_ * frameBytes() / 1000.0;
    const double fillMs = (sink_->bufferSize() - sink_->bytesFree()) / bytesPerMs;
    fillMsAvg_ = (fillMsAvg_ < 0.0) ? fillMs
                                    : fillMsAvg_ + kFillAvgAlpha * (fillMs - fillMsAvg_);
    const double fillErr = std::clamp((fillMsAvg_ - kTargetMs) / kTargetMs, -1.0, 1.0);
    const double outRateTrimmed = outRate_ * (1.0 - kMaxRateTrim * fillErr);

    // Resample demodulator SR → device output rate
    const QVector<float> rs = resampler_.process(samples, sampleRateHz, outRateTrimmed);
    if (rs.isEmpty()) return;

    // ── AGC: compute block RMS, update gain ───────────────────────────────────
    // RMS of current block
    float rms = 0.0f;
    for (float s : rs) rms += s * s;
    rms = std::sqrt(rms / static_cast<float>(rs.size()));

    // Adjust gain toward target; use fast attack / slow release
    if (rms > 0.0001f) {
        const float desiredGain = kAgcTarget / rms;
        const float alpha = (desiredGain < agcGain_) ? kAgcAttack : kAgcRelease;
        agcGain_ = agcGain_ + alpha * (desiredGain - agcGain_);
        agcGain_ = std::clamp(agcGain_, kAgcMin, kAgcMax);
    }

    // Diagnostic every 100 blocks
    if (++diagCount_ % 100 == 0) {
        float peak = 0.0f;
        for (float s : rs) peak = std::max(peak, std::abs(s));
        LOG_DEBUG("FmAudioOutput: rms=" + std::to_string(rms)
                  + " agcGain=" + std::to_string(agcGain_)
                  + " peak_in=" + std::to_string(peak)
                  + " peak_out=" + std::to_string(std::min(peak * agcGain_, 1.0f))
                  + " blocks=" + std::to_string(diagCount_));
    }

    if (outIsFloat_) {
        QVector<float> pcm(rs.size() * 2);
        for (int i = 0; i < rs.size(); ++i) {
            const float s = std::clamp(rs[i] * agcGain_, -1.0f, 1.0f);
            pcm[2 * i]     = s;
            pcm[2 * i + 1] = s;
        }
        writePcm(reinterpret_cast<const char*>(pcm.constData()),
                 static_cast<qint64>(pcm.size()) * sizeof(float));
    } else {
        QVector<int16_t> pcm(rs.size() * 2);
        for (int i = 0; i < rs.size(); ++i) {
            const float s = std::clamp(rs[i] * agcGain_, -1.0f, 1.0f);
            const auto  v = static_cast<int16_t>(s * 32767.0f);
            pcm[2 * i]     = v;
            pcm[2 * i + 1] = v;
        }
        writePcm(reinterpret_cast<const char*>(pcm.constData()),
                 static_cast<qint64>(pcm.size()) * sizeof(int16_t));
    }
}

// ---------------------------------------------------------------------------
// writePcm — latency-bounded write (see header, "Latency control")
// ---------------------------------------------------------------------------
void FmAudioOutput::writePcm(const char* data, qint64 bytes) {
    const qint64 fb         = frameBytes();
    const qint64 bytesPerMs = outRate_ * fb / 1000;
    const qint64 bytesFree  = sink_->bytesFree();
    const qint64 fill       = sink_->bufferSize() - bytesFree;

    if (fill > kHighWaterMs * bytesPerMs)
        draining_ = true;
    if (draining_) {
        if (fill > kTargetMs * bytesPerMs) {
            droppedBytes_ += bytes;
            return;
        }
        draining_ = false;
    }

    const qint64 toWrite = std::min(bytes, bytesFree / fb * fb);
    const qint64 written = toWrite > 0 ? device_->write(data, toWrite) : 0;
    if (written < bytes)
        truncatedBytes_ += bytes - std::max<qint64>(written, 0);
}

// ---------------------------------------------------------------------------
// openSink — always forces Float32 stereo (matches Win10/11 WASAPI default)
// ---------------------------------------------------------------------------
bool FmAudioOutput::openSink(double sampleRateHz) {
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        const QString msg = "FmAudioOutput: no audio output device found";
        LOG_ERROR(msg.toStdString());
        emit statusChanged(msg, true);
        return false;
    }

    // Log preferred format for diagnostics
    const QAudioFormat pref = dev.preferredFormat();
    LOG_INFO("FmAudioOutput: device preferred format: "
             + std::to_string(pref.sampleRate()) + " Hz "
             + std::to_string(pref.channelCount()) + "ch fmt="
             + std::to_string(static_cast<int>(pref.sampleFormat())));

    // Try Float32 stereo at preferred rate first — this matches what WASAPI
    // actually wants on Win10/11 and avoids any silent format conversion.
    QAudioFormat fmt;
    fmt.setSampleRate(pref.sampleRate());
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);

    if (!dev.isFormatSupported(fmt)) {
        // Try Int16 at preferred rate
        fmt.setSampleFormat(QAudioFormat::Int16);
        if (!dev.isFormatSupported(fmt)) {
            // Last resort: 48 kHz Int16 stereo
            fmt.setSampleRate(48'000);
            fmt.setSampleFormat(QAudioFormat::Int16);
        }
    }

    outRate_    = fmt.sampleRate();
    outIsFloat_ = (fmt.sampleFormat() == QAudioFormat::Float);

    LOG_INFO("FmAudioOutput: opening sink — "
             + std::to_string(outRate_) + " Hz stereo "
             + (outIsFloat_ ? "Float32" : "Int16"));

    sink_ = new QAudioSink(dev, fmt, this);

    // Buffer = 300 ms
    const int bytesPerStereoSample = 2 * (outIsFloat_ ? 4 : 2);
    sink_->setBufferSize(
        static_cast<qsizetype>(outRate_ * bytesPerStereoSample * 0.3));
    sink_->setVolume(volume_);

    device_ = sink_->start();

    if (!device_ || sink_->state() == QAudio::StoppedState) {
        const QString errStr = [this]() -> QString {
            switch (sink_->error()) {
                case QAudio::OpenError:     return "OpenError";
                case QAudio::IOError:       return "IOError";
                case QAudio::UnderrunError: return "UnderrunError";
                case QAudio::FatalError:    return "FatalError";
                default:                    return "NoError/Unknown";
            }
        }();
        const QString msg = QString("FmAudioOutput: QAudioSink failed — %1").arg(errStr);
        LOG_ERROR(msg.toStdString());
        emit statusChanged(msg, true);
        delete sink_;
        sink_   = nullptr;
        device_ = nullptr;
        return false;
    }

    statusText_ = QString("FM ♪  |  demod %1 Hz → %2 Hz stereo %3")
        .arg(static_cast<int>(sampleRateHz))
        .arg(outRate_)
        .arg(outIsFloat_ ? "Float32" : "Int16");
    LOG_INFO("FmAudioOutput: sink OK — " + statusText_.toStdString());
    emit statusChanged(statusText_, false);
    return true;
}

// ---------------------------------------------------------------------------
// LinearResampler::process
// ---------------------------------------------------------------------------
QVector<float> FmAudioOutput::LinearResampler::process(
    const QVector<float>& in, double inRate, double outRate)
{
    if (in.isEmpty() || inRate <= 0 || outRate <= 0) return {};

    const double step = inRate / outRate;
    QVector<float> out;
    out.reserve(static_cast<int>(in.size() * outRate / inRate) + 2);

    while (true) {
        const int i = static_cast<int>(phase);
        if (i >= in.size()) {
            phase -= in.size();
            prev = in.back();
            break;
        }
        const float s0   = (i == 0) ? prev : in[i - 1];
        const float s1   = in[i];
        const float frac = static_cast<float>(phase - static_cast<double>(i));
        out.push_back(s0 + frac * (s1 - s0));
        phase += step;
    }
    return out;
}
