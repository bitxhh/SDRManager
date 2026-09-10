#pragma once

#include "../Core/IDeviceManager.h"

#include <QElapsedTimer>

#include <mutex>

// ---------------------------------------------------------------------------
// SoapyDeviceManager — обнаружение устройств через SoapySDR (SoapyRTLSDR,
// SoapyAirspy, ...). Если SoapySDR.dll отсутствует — список всегда пуст.
//
// Энумерация идёт по белому списку драйверов (kDrivers в .cpp), по одному
// за вызов: нефильтрованный enumerate в SoapySDR 0.8 запускает find() всех
// модулей параллельно, что роняет rtlsdr на общей libusb (см. .cpp).
// "lime" в списке нет — LimeSDR обслуживает нативный LimeDeviceManager;
// "audio" (звуковые карты SoapyAudio) тоже не энумерируется.
//
// Особенность rtlsdr: устройство с занятым USB-хэндлом (открытое нами же)
// пропадает из enumerate. Поэтому открытые устройства (state != Connected)
// остаются в списке, даже если enumerate их не вернул — иначе watchdog
// DeviceDetailWindow принудительно закрыл бы окно во время работы.
// Более того, драйвер открытого устройства вообще не энумерируется, пока
// оно открыто: find() лишь печатал бы [ERROR] о занятом хэндле каждый цикл.
// ---------------------------------------------------------------------------
class SoapyDeviceManager : public IDeviceManager {
    Q_OBJECT

public:
    explicit SoapyDeviceManager(QObject* parent = nullptr);

    void refresh() override;
    [[nodiscard]] QList<std::shared_ptr<IDevice>> devices() const override;

private:
    mutable std::mutex mutex_;
    QElapsedTimer enumTimer_;   // троттлинг энумерации; под mutex_
    QList<std::shared_ptr<IDevice>> devices_;
};
