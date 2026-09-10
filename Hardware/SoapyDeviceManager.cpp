#include "SoapyDeviceManager.h"
#include "SoapyApi.h"
#include "SoapyDevice.h"
#include "Logger.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

// Окно выбора устройств зовёт refresh() каждые 500 мс, watchdog — каждую
// секунду. Каждая энумерация SoapyRTLSDR открывает донгл по USB (чтение
// строк дескриптора); гонка с surprise removal роняет librtlsdr/libusb
// (heap corruption внутри MSVC DLL). Троттлинг сужает окно гонки.
constexpr qint64 kEnumIntervalMs = 3000;

// SoapySDR 0.8 при энумерации БЕЗ фильтра запускает find() всех модулей
// параллельно (std::async). Модули uhd/osmosdr дёргают ту же libusb-1.0.dll
// (в PothosSDR — 1.0.23, чей Windows-бэкенд не переживает конкурентные
// контексты): rtlsdr_get_device_usb_strings стабильно падает, а surprise
// removal даёт heap corruption (0xC0000374). Воспроизводится и в чистом
// `SoapySDRUtil --find`; с фильтром `--find="driver=rtlsdr"` — работает.
// Поэтому энумерируем только известные драйверы, по одному, последовательно:
// с фильтром driver= SoapySDR зовёт find() единственного модуля.
constexpr const char* kDrivers[] = {
    "rtlsdr", "airspy", "airspyhf", "hackrf", "bladerf", "sdrplay", "plutosdr",
};

struct FoundDevice {
    QString makeArgs;   // "driver=rtlsdr,serial=00000001"
    QString id;
    QString name;
};

// id = "soapy:<driver>:<serial>" → "<driver>"
QString driverOfId(const QString& id) {
    const QStringList parts = id.split(QLatin1Char(':'));
    return parts.size() >= 2 ? parts[1] : QString();
}

QList<FoundDevice> enumerateSoapy(const QSet<QString>& skipDrivers) {
    QList<FoundDevice> found;
    const auto& a = soapy::api();
    if (!a.loaded) return found;

    for (const char* drv : kDrivers) {
        // Пока наше устройство этого драйвера открыто, его USB-хэндл занят:
        // find() не сможет прочитать дескрипторы и лишь напечатает [ERROR].
        if (skipDrivers.contains(QLatin1StringView(drv)))
            continue;

        const QByteArray filter = QByteArrayLiteral("driver=") + drv;

        size_t          n    = 0;
        SoapySDRKwargs* list = a.enumerateStrArgs(filter.constData(), &n);
        if (!list) continue;

        for (size_t i = 0; i < n; ++i) {
            const SoapySDRKwargs& kw = list[i];

            QString driver = QString::fromLatin1(drv);
            QString serial, label;
            QStringList makeParts;
            for (size_t k = 0; k < kw.size; ++k) {
                const QString key = QString::fromUtf8(kw.keys[k]);
                const QString val = QString::fromUtf8(kw.vals[k]);
                if (key == QLatin1String("driver")) driver = val;
                if (key == QLatin1String("serial")) serial = val;
                if (key == QLatin1String("label"))  label  = val;
                // label может содержать запятые/пробелы — в make-строку не включаем
                if (key != QLatin1String("label"))
                    makeParts << key + QLatin1Char('=') + val;
            }

            FoundDevice dev;
            dev.makeArgs = makeParts.join(QLatin1Char(','));
            dev.id       = QStringLiteral("soapy:") + driver + QLatin1Char(':')
                         + (serial.isEmpty() ? QString::number(i) : serial);
            dev.name     = label.isEmpty() ? QStringLiteral("SoapySDR ") + driver : label;
            found.append(dev);
        }

        a.kwargsListClear(list, n);
    }

    return found;
}

} // namespace

SoapyDeviceManager::SoapyDeviceManager(QObject* parent)
    : IDeviceManager(parent)
{
    refresh();
}

void SoapyDeviceManager::refresh() {
    QSet<QString> skipDrivers;
    {
        std::lock_guard lock(mutex_);
        if (enumTimer_.isValid() && enumTimer_.elapsed() < kEnumIntervalMs)
            return;
        enumTimer_.start();

        // Драйверы, чьё устройство открыто нами (state != Connected),
        // не энумерируем: их find() упрётся в наш же занятый USB-хэндл.
        for (const auto& d : devices_)
            if (d->state() != DeviceState::Connected)
                skipDrivers.insert(driverOfId(d->id()));
    }

    const QList<FoundDevice> found = enumerateSoapy(skipDrivers);

    bool changed = false;
    {
        std::lock_guard lock(mutex_);

        QSet<QString> foundIds;
        for (const auto& f : found) foundIds.insert(f.id);

        // Удаляем исчезнувшие. Открытые (state != Connected) оставляем:
        // занятый USB-хэндл rtlsdr скрывает устройство от enumerate.
        // Устройства пропущенных драйверов тоже оставляем — их не искали.
        for (auto it = devices_.begin(); it != devices_.end();) {
            if (!foundIds.contains((*it)->id())
                && (*it)->state() == DeviceState::Connected
                && !skipDrivers.contains(driverOfId((*it)->id()))) {
                LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Info,
                        "SoapyDeviceManager: device lost: " + (*it)->id().toStdString());
                it = devices_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }

        // Добавляем новые.
        for (const auto& f : found) {
            const bool known = std::any_of(devices_.cbegin(), devices_.cend(),
                [&f](const auto& d) { return d->id() == f.id; });
            if (known) continue;

            devices_.append(std::make_shared<SoapyDevice>(f.makeArgs, f.id, f.name));
            LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Info,
                    "SoapyDeviceManager: device found: " + f.id.toStdString()
                    + " (" + f.makeArgs.toStdString() + ")");
            changed = true;
        }
    }

    if (changed)
        emit devicesChanged();
}

QList<std::shared_ptr<IDevice>> SoapyDeviceManager::devices() const {
    std::lock_guard lock(mutex_);
    return devices_;
}
