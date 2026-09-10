#pragma once

#include "../Core/IDevice.h"

#include <QElapsedTimer>
#include <QFile>
#include <atomic>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// FileDevice — реализация IDevice, воспроизводящая записанный I/Q файл
// как живое устройство.
//
// Форматы (по расширению):
//   .cf32 — interleaved float32 [I0, Q0, I1, Q1, ...] (формат записи SDRManager)
//   .cs16 — interleaved int16  [I0, Q0, I1, Q1, ...]
//
// Метаданные (частота, sample rate) извлекаются из имени файла по конвенции
// FileNaming: {date}_{time}_{source}_{freq}MHz_{rate}MSps.cf32
//
// readBlock() темпирует выдачу под sample rate (QElapsedTimer), по концу
// файла зацикливается на начало. Timestamp — монотонный счётчик семплов.
// ---------------------------------------------------------------------------
class FileDevice : public IDevice {
    Q_OBJECT

public:
    explicit FileDevice(const QString& path, QObject* parent = nullptr);
    ~FileDevice() override;

    FileDevice(const FileDevice&)            = delete;
    FileDevice& operator=(const FileDevice&) = delete;

    // Единый формат ID для поиска дубликатов в CompositeDeviceManager.
    static QString makeId(const QString& path);

    // ── IDevice: идентификация ────────────────────────────────────────────────
    [[nodiscard]] QString id()   const override;
    [[nodiscard]] QString name() const override;

    // ── IDevice: жизненный цикл ───────────────────────────────────────────────
    void init(const QList<ChannelDescriptor>& channels = {}) override;
    void close() override;

    // ── IDevice: параметры ────────────────────────────────────────────────────
    void   setSampleRate(double hz)                      override;
    [[nodiscard]] double sampleRate()              const override;
    [[nodiscard]] QList<double> supportedSampleRates()   const override;

    void   setFrequency(double hz)                       override;
    [[nodiscard]] double frequency()               const override;

    void   setGain(double dB)                            override;
    [[nodiscard]] double gain()                    const override;
    [[nodiscard]] double maxGain()                 const override { return 0.0; }

    // ── IDevice: стрим ────────────────────────────────────────────────────────
    void startStream() override;
    void stopStream()  override;
    int  readBlock(int16_t* buffer, int count, int timeoutMs) override;

    [[nodiscard]] uint64_t lastReadTimestamp(ChannelDescriptor ch) const override;

    // ── IDevice: состояние / возможности ─────────────────────────────────────
    [[nodiscard]] DeviceState state() const override { return state_; }
    [[nodiscard]] QList<ChannelInfo> availableChannels() const override {
        return { {{ChannelDescriptor::RX, 0}, QStringLiteral("File")} };
    }

private:
    enum class SampleFormat { CF32, CS16 };

    void setState(DeviceState s);
    void parseFileName();

    QString      path_;
    SampleFormat format_{SampleFormat::CF32};
    int          bytesPerPair_{8};

    // Файл читается только из воркер-потока; open/close — из UI-потока
    // (воркер к этому моменту остановлен RxController'ом).
    QFile              file_;
    mutable std::mutex fileMutex_;
    std::vector<float> floatScratch_;   // временный буфер для cf32 → int16

    double parsedRateHz_{0.0};   // из имени файла, 0 = не распознано
    double parsedFreqHz_{0.0};

    std::atomic<double> sampleRateHz_{2'000'000.0};
    std::atomic<double> frequencyHz_{102e6};
    std::atomic<double> gainDb_{0.0};

    // Пейсинг реального времени: target(нс) = samplesDelivered_ / rate.
    QElapsedTimer         pacer_;
    uint64_t              samplesDelivered_{0};   // только воркер-поток
    std::atomic<uint64_t> lastTimestamp_{0};

    DeviceState state_{DeviceState::Connected};
};
