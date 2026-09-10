#include "CompositeDeviceManager.h"
#include "FileDevice.h"
#include "Logger.h"

#include <exception>

CompositeDeviceManager::CompositeDeviceManager(QObject* parent)
    : IDeviceManager(parent)
{
    connect(&lime_,  &IDeviceManager::devicesChanged,
            this,    &IDeviceManager::devicesChanged);
    connect(&soapy_, &IDeviceManager::devicesChanged,
            this,    &IDeviceManager::devicesChanged);
}

void CompositeDeviceManager::refresh() {
    // refresh() выполняется в QtConcurrent (DeviceSelectionWindow/watchdog);
    // исключение перебросилось бы в UI-поток через QFuture::result().
    try {
        lime_.refresh();
    } catch (const std::exception& e) {
        LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Error,
                std::string("CompositeDeviceManager: Lime refresh failed: ") + e.what());
    }
    try {
        soapy_.refresh();
    } catch (const std::exception& e) {
        LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Error,
                std::string("CompositeDeviceManager: Soapy refresh failed: ") + e.what());
    }
}

QList<std::shared_ptr<IDevice>> CompositeDeviceManager::devices() const {
    QList<std::shared_ptr<IDevice>> all = lime_.devices();
    all += soapy_.devices();
    {
        std::lock_guard lock(fileMutex_);
        all += fileDevices_;
    }
    return all;
}

std::shared_ptr<IDevice> CompositeDeviceManager::openFile(const QString& path) {
    const QString id = FileDevice::makeId(path);
    std::shared_ptr<IDevice> dev;
    {
        std::lock_guard lock(fileMutex_);
        for (const auto& existing : fileDevices_)
            if (existing->id() == id)
                return existing;

        dev = std::make_shared<FileDevice>(path);
        fileDevices_.append(dev);
        LOG_CAT(LogCat::kDeviceLifecycle, LogLevel::Info,
                "CompositeDeviceManager: file device added: " + path.toStdString());
    }
    emit devicesChanged();
    return dev;
}
