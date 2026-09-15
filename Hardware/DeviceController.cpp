#include "DeviceController.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <QtConcurrent/QtConcurrent>

DeviceController::DeviceController(std::shared_ptr<IDevice> device, QObject* parent)
    : QObject(parent)
    , device_(std::move(device))
{}

void DeviceController::initDevice(const QList<ChannelDescriptor>& channels) {
    try {
        device_->init(channels);
        emit deviceInitialized();
        emit sampleRateChanged(device_->sampleRate());
        emit statusChanged(
            QString("Initialized — %1 Hz").arg(device_->sampleRate(), 0, 'f', 0));
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    }
}

void DeviceController::calibrate(const QList<ChannelDescriptor>& channels, double calBwHz) {
    try {
        device_->calibrate(channels, calBwHz);
        emit sampleRateChanged(device_->sampleRate());
        emit statusChanged(
            QString("Calibrated at %1 Hz").arg(device_->sampleRate(), 0, 'f', 0));
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    }
}

void DeviceController::setSampleRate(double sampleRateHz) {
    try {
        device_->setSampleRate(sampleRateHz);
        emit sampleRateChanged(device_->sampleRate());
        emit statusChanged(
            QString("Sample rate: %1 Hz").arg(device_->sampleRate(), 0, 'f', 0));
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    }
}

void DeviceController::setGain(double dB) {
    try {
        device_->setGain(dB);
        emit statusChanged(QString("Gain: %1 dB").arg(dB, 0, 'f', 1));
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    }
}

void DeviceController::setGainChannel(ChannelDescriptor ch, double dB) {
    try {
        device_->setGain(ch, dB);
        const QString dir = (ch.direction == ChannelDescriptor::TX) ? "TX" : "RX";
        emit statusChanged(QString("%1%2 Gain: %3 dB")
                           .arg(dir).arg(ch.channelIndex).arg(dB, 0, 'f', 1));
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    }
}

void DeviceController::setFrequency(double freqMHz) {
    try {
        device_->setFrequency(freqMHz * 1e6);
        emit statusChanged(QString("Centre: %1 MHz").arg(freqMHz, 0, 'f', 3));
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    }
}

void DeviceController::setFrequencyChannel(ChannelDescriptor ch, double freqMHz) {
    try {
        device_->setFrequency(ch, freqMHz * 1e6);
        emit statusChanged(QString("RX%1: %2 MHz")
                           .arg(ch.channelIndex).arg(freqMHz, 0, 'f', 3));
    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    }
}

void DeviceController::reconfigureChannels(const QList<ChannelDescriptor>& channels,
                                           const QMap<int, double>& rxGainsDb) {
    (void)QtConcurrent::run([this, channels, rxGainsDb]() {
        try {
            emit progressChanged(0, "Reconfiguring channels…");
            device_->reconfigureChannels(channels);

            // Newly enabled channels come up at the driver default gain (0 dB on
            // Lime) — push the UI value. setGain() re-calibrates the channel.
            for (const auto& ch : channels) {
                if (ch.direction != ChannelDescriptor::RX || !rxGainsDb.contains(ch.channelIndex))
                    continue;
                const double dB = rxGainsDb.value(ch.channelIndex);
                if (std::abs(device_->gain(ch) - dB) > 0.5) {
                    emit progressChanged(50, QString("Applying RX%1 gain…").arg(ch.channelIndex));
                    device_->setGain(ch, dB);
                }
            }

            emit sampleRateChanged(device_->sampleRate());
            emit progressChanged(100, QString("Ready — %1 Hz").arg(device_->sampleRate(), 0, 'f', 0));
            emit deviceInitialized();
        } catch (const std::exception& ex) {
            emit errorOccurred(QString::fromStdString(ex.what()));
        }
    });
}

void DeviceController::autoOpen(const QList<ChannelDescriptor>& channels, double sampleRateHz,
                                const QMap<int, double>& rxGainsDb) {
    (void)QtConcurrent::run([this, channels, sampleRateHz, rxGainsDb]() {
        try {
            emit progressChanged(0, "Initializing…");
            device_->init(channels);
            emit deviceInitialized();
            emit sampleRateChanged(device_->sampleRate());

            emit progressChanged(15, "Setting sample rate…");
            device_->setSampleRate(sampleRateHz);
            emit sampleRateChanged(device_->sampleRate());

            emit progressChanged(25, "Preparing calibration…");

            QList<ChannelDescriptor> rxChs;
            for (const auto& ch : channels)
                if (ch.direction == ChannelDescriptor::RX)
                    rxChs.append(ch);

            if (rxChs.isEmpty()) {
                emit progressChanged(30, "Calibrating…");
                device_->calibrate({}, -1.0);
            } else {
                for (int i = 0; i < rxChs.size(); ++i) {
                    const int pct = (rxChs.size() == 1) ? 30 : 30 + i * 35;
                    const int idx = rxChs[i].channelIndex;
                    emit progressChanged(pct, QString("Calibrating RX%1…").arg(idx));
                    // Calibration is gain-dependent, so the saved gain must be in
                    // the chip before LMS_Calibrate. On Lime, setGain() in Ready
                    // state calibrates the channel itself — calling calibrate()
                    // afterwards would just repeat it. Soapy/File: calibrate() is
                    // a no-op, setGain() is all that matters.
                    if (rxGainsDb.contains(idx))
                        device_->setGain(rxChs[i], rxGainsDb.value(idx));
                    else
                        device_->calibrate({rxChs[i]}, -1.0);
                }
            }

            emit sampleRateChanged(device_->sampleRate());
            emit progressChanged(100, QString("Ready — %1 Hz").arg(device_->sampleRate(), 0, 'f', 0));
        } catch (const std::exception& ex) {
            emit errorOccurred(QString::fromStdString(ex.what()));
        }
    });
}
