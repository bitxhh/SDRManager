#pragma once

#include "../Core/IDevice.h"
#include "SoapyApi.h"

#include <atomic>
#include <mutex>

// ---------------------------------------------------------------------------
// SoapyDevice — реализация IDevice поверх SoapySDR C API.
//
// Позволяет работать с любым железом, для которого установлен Soapy-модуль
// (RTL-SDR/R820T2 через SoapyRTLSDR, Airspy, HackRF, ...). Native LimeSDR
// обслуживается LimeDeviceManager — драйвер "lime" здесь отфильтрован.
//
// Стрим: один канал RX0, формат CS16 (SoapyRTLSDR отдаёт (CU8-128)*256,
// т.е. полную шкалу ±32768 — совпадает с нормировкой /32768 в RxWorker).
//
// setFrequency() на живом стриме НЕ эмитирует retuned(): у SoapyDevice нет
// park-handshake, а обработчики retuned сбрасывают DSP-состояние в
// DirectConnection и гонялись бы с воркером.
// ---------------------------------------------------------------------------
class SoapyDevice : public IDevice {
    Q_OBJECT

public:
    SoapyDevice(const QString& makeArgs, const QString& id,
                const QString& name, QObject* parent = nullptr);
    ~SoapyDevice() override;

    SoapyDevice(const SoapyDevice&)            = delete;
    SoapyDevice& operator=(const SoapyDevice&) = delete;

    // ── IDevice: идентификация ────────────────────────────────────────────────
    [[nodiscard]] QString id()   const override { return id_; }
    [[nodiscard]] QString name() const override { return name_; }

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
    [[nodiscard]] double maxGain()                 const override;

    // ── IDevice: стрим ────────────────────────────────────────────────────────
    void startStream() override;
    void stopStream()  override;
    int  readBlock(int16_t* buffer, int count, int timeoutMs) override;

    [[nodiscard]] uint64_t lastReadTimestamp(ChannelDescriptor ch) const override;

    // ── IDevice: состояние / возможности ─────────────────────────────────────
    [[nodiscard]] DeviceState state() const override { return state_; }
    [[nodiscard]] QList<ChannelInfo> availableChannels() const override {
        return { {{ChannelDescriptor::RX, 0}, QStringLiteral("RX0")} };
    }

private:
    void setState(DeviceState s);
    [[nodiscard]] QString soapyError() const;

    QString makeArgs_;   // строка для SoapySDRDevice_makeStrArgs ("driver=rtlsdr,...")
    QString id_;
    QString name_;

    // dev_/stream_ трогают UI-поток (init/close/set*) и воркер (readBlock,
    // start/stopStream). Конфигурационные вызовы сериализуются apiMutex_;
    // readStream потокобезопасен внутри Soapy-драйверов и идёт без лока,
    // чтобы setGain из UI не блокировался на 100 мс таймаута чтения.
    SoapySDRDevice* dev_{nullptr};
    SoapySDRStream* stream_{nullptr};
    mutable std::mutex apiMutex_;

    QList<double> rates_;          // кэш listSampleRates после init
    double        gainMin_{0.0};
    double        gainMax_{49.6};  // дефолт под R820T2, уточняется в init

    std::atomic<double> sampleRateHz_{2'000'000.0};
    std::atomic<double> frequencyHz_{102e6};
    std::atomic<double> gainDb_{0.0};

    std::atomic<uint64_t> samplesDelivered_{0};

    DeviceState state_{DeviceState::Connected};
};
