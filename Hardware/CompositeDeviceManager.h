#pragma once

#include "../Core/IDeviceManager.h"
#include "LimeDeviceManager.h"
#include "SoapyDeviceManager.h"

#include <mutex>

// ---------------------------------------------------------------------------
// CompositeDeviceManager — объединяет источники устройств в один список:
//   * LimeDeviceManager  — нативные LimeSDR (LimeSuite),
//   * SoapyDeviceManager — всё, что видит SoapySDR (RTL-SDR и др.),
//   * FileDevice         — открытые пользователем I/Q файлы (openFile).
//
// FileDevice'ы остаются в списке после закрытия окна (файл не «отключается»),
// что позволяет открыть его повторно и не даёт watchdog'у DeviceDetailWindow
// закрыть окно во время воспроизведения.
// ---------------------------------------------------------------------------
class CompositeDeviceManager : public IDeviceManager {
    Q_OBJECT

public:
    explicit CompositeDeviceManager(QObject* parent = nullptr);

    void refresh() override;
    [[nodiscard]] QList<std::shared_ptr<IDevice>> devices() const override;

    // Открыть I/Q файл (.cf32/.cs16) как устройство. Повторный вызов с тем же
    // путём возвращает уже существующий FileDevice.
    std::shared_ptr<IDevice> openFile(const QString& path) override;

private:
    // Без QObject-парентинга: время жизни управляется значением члена,
    // парент привёл бы к двойному удалению.
    LimeDeviceManager  lime_;
    SoapyDeviceManager soapy_;

    mutable std::mutex fileMutex_;
    QList<std::shared_ptr<IDevice>> fileDevices_;
};
