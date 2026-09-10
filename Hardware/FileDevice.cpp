#include "FileDevice.h"
#include "Logger.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
FileDevice::FileDevice(const QString& path, QObject* parent)
    : IDevice(parent)
    , path_(path)
{
    const QString ext = QFileInfo(path_).suffix().toLower();
    if (ext == QLatin1String("cs16")) {
        format_       = SampleFormat::CS16;
        bytesPerPair_ = 4;   // 2 × int16
    } else {
        format_       = SampleFormat::CF32;
        bytesPerPair_ = 8;   // 2 × float32
    }
    parseFileName();
    if (parsedRateHz_ > 0.0) sampleRateHz_ = parsedRateHz_;
    if (parsedFreqHz_ > 0.0) frequencyHz_  = parsedFreqHz_;
}

FileDevice::~FileDevice() {
    std::lock_guard lock(fileMutex_);
    if (file_.isOpen()) file_.close();
}

// ---------------------------------------------------------------------------
// Identification
// ---------------------------------------------------------------------------
QString FileDevice::makeId(const QString& path) {
    return QStringLiteral("file:") + path;
}

QString FileDevice::id() const   { return makeId(path_); }
QString FileDevice::name() const {
    return QStringLiteral("File: ") + QFileInfo(path_).fileName();
}

// ---------------------------------------------------------------------------
// Filename metadata: {date}_{time}_{source}_{102.000}MHz_{4.000}MSps.cf32
// ---------------------------------------------------------------------------
void FileDevice::parseFileName() {
    const QString base = QFileInfo(path_).fileName();

    static const QRegularExpression freqRe(
        QStringLiteral("([0-9]+(?:\\.[0-9]+)?)MHz"));
    static const QRegularExpression rateRe(
        QStringLiteral("([0-9]+(?:\\.[0-9]+)?)([Mk])Sps"));

    if (const auto m = freqRe.match(base); m.hasMatch())
        parsedFreqHz_ = m.captured(1).toDouble() * 1e6;

    if (const auto m = rateRe.match(base); m.hasMatch()) {
        const double mult = m.captured(2) == QLatin1String("M") ? 1e6 : 1e3;
        parsedRateHz_ = m.captured(1).toDouble() * mult;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void FileDevice::init(const QList<ChannelDescriptor>& /*channels*/) {
    std::lock_guard lock(fileMutex_);

    if (!file_.isOpen()) {
        file_.setFileName(path_);
        if (!file_.open(QIODevice::ReadOnly))
            throw std::runtime_error("FileDevice: cannot open file: "
                                     + path_.toStdString());
    }
    if (file_.size() < bytesPerPair_)
        throw std::runtime_error("FileDevice: file is empty or too small: "
                                 + path_.toStdString());
    file_.seek(0);

    LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Info,
            "FileDevice: opened " + path_.toStdString()
            + " (rate=" + std::to_string(sampleRateHz_.load())
            + " Hz, freq=" + std::to_string(frequencyHz_.load()) + " Hz)");

    setState(DeviceState::Ready);
}

void FileDevice::close() {
    {
        std::lock_guard lock(fileMutex_);
        if (file_.isOpen()) file_.close();
    }
    lastTimestamp_ = 0;
    setState(DeviceState::Connected);
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
void FileDevice::setSampleRate(double hz) {
    if (hz <= 0.0)
        throw std::runtime_error("FileDevice: invalid sample rate");
    sampleRateHz_ = hz;
    emit sampleRateChanged(hz);
}

double FileDevice::sampleRate() const { return sampleRateHz_; }

QList<double> FileDevice::supportedSampleRates() const {
    if (parsedRateHz_ > 0.0)
        return { parsedRateHz_ };
    // Имя файла не содержит rate — предлагаем типовые значения.
    return { 250e3, 1e6, 2e6, 4e6, 8e6, 10e6, 15e6, 20e6 };
}

void FileDevice::setFrequency(double hz) { frequencyHz_ = hz; }
double FileDevice::frequency() const     { return frequencyHz_; }

void FileDevice::setGain(double dB) { gainDb_ = dB; }
double FileDevice::gain() const     { return gainDb_; }

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------
void FileDevice::startStream() {
    {
        std::lock_guard lock(fileMutex_);
        if (!file_.isOpen())
            throw std::runtime_error("FileDevice: startStream before init()");
        file_.seek(0);
    }
    samplesDelivered_ = 0;
    lastTimestamp_    = 0;
    pacer_.start();
    setState(DeviceState::Streaming);
}

void FileDevice::stopStream() {
    if (state_ == DeviceState::Streaming)
        setState(DeviceState::Ready);
}

int FileDevice::readBlock(int16_t* buffer, int count, int /*timeoutMs*/) {
    if (count <= 0) return 0;

    // ── Пейсинг: не отдавать данные быстрее sample rate ──────────────────────
    const double rate = sampleRateHz_.load();
    if (rate > 0.0 && pacer_.isValid()) {
        const auto targetNs =
            static_cast<qint64>(static_cast<double>(samplesDelivered_) * 1e9 / rate);
        const qint64 aheadNs = targetNs - pacer_.nsecsElapsed();
        if (aheadNs > 0)
            QThread::usleep(static_cast<unsigned long>(aheadNs / 1000));
    }

    // ── Чтение с зацикливанием на начало файла ────────────────────────────────
    std::lock_guard lock(fileMutex_);
    if (!file_.isOpen()) return -1;

    const int totalBytes = count * bytesPerPair_;
    char*     dst        = nullptr;

    if (format_ == SampleFormat::CS16) {
        dst = reinterpret_cast<char*>(buffer);
    } else {
        floatScratch_.resize(static_cast<std::size_t>(count) * 2);
        dst = reinterpret_cast<char*>(floatScratch_.data());
    }

    int done = 0;
    while (done < totalBytes) {
        const qint64 n = file_.read(dst + done, totalBytes - done);
        if (n < 0) return -1;
        if (n == 0) {
            if (!file_.seek(0)) return -1;   // EOF → loop
            continue;
        }
        done += static_cast<int>(n);
    }

    if (format_ == SampleFormat::CF32) {
        for (int i = 0; i < count * 2; ++i) {
            const float v = std::clamp(floatScratch_[static_cast<std::size_t>(i)]
                                       * 32767.0f, -32768.0f, 32767.0f);
            buffer[i] = static_cast<int16_t>(v);
        }
    }

    samplesDelivered_ += static_cast<uint64_t>(count);
    lastTimestamp_     = samplesDelivered_;
    return count;
}

uint64_t FileDevice::lastReadTimestamp(ChannelDescriptor /*ch*/) const {
    return lastTimestamp_;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void FileDevice::setState(DeviceState s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}
