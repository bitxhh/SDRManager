#include "ModemHandler.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>

ModemHandler::ModemHandler(double stationOffsetHz, QObject* parent)
    : QObject(parent)
    , stationOffsetHz_(stationOffsetHz)
    , currentOffsetHz_(stationOffsetHz)
{}

int ModemHandler::tapsParam(const std::map<QString, double>& params,
                            const QString& key, int def) {
    auto it = params.find(key);
    int taps = it != params.end() ? static_cast<int>(std::lround(it->second)) : def;
    taps = std::clamp(taps, kMinFirTaps, kMaxFirTaps);
    return taps | 1;
}

bool ModemHandler::isTapsParam(const QString& name) {
    return name.endsWith(QStringLiteral("taps"), Qt::CaseInsensitive);
}

void ModemHandler::setParam(const QString& name, double value) {
    std::lock_guard lock(paramMutex_);
    params_[name] = value;
    pendingParams_.emplace_back(name, value);
}

double ModemHandler::param(const QString& name) const {
    std::lock_guard lock(paramMutex_);
    auto it = params_.find(name);
    return it != params_.end() ? it->second : 0.0;
}

void ModemHandler::setOffset(double hz) {
    pendingOffset_.store(hz);
}

void ModemHandler::onStreamStarted(double sampleRateHz) {
    std::map<QString, double> paramsCopy;
    {
        std::lock_guard lock(paramMutex_);
        paramsCopy = params_;
        pendingParams_.clear();
    }
    try {
        dem_ = createDemodulator(sampleRateHz, currentOffsetHz_, paramsCopy);
        LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
                std::string(modemName()) + ": ready — audio SR="
                + std::to_string(static_cast<int>(dem_->audioSampleRate())) + " Hz");
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string(modemName()) + " init failed: " + ex.what());
        dem_.reset();
    }
}

void ModemHandler::onStreamStopped() {
    dem_.reset();
}

void ModemHandler::processBlock(const float* iq, int count, double sampleRateHz) {
    // Lazy-init: handler may have been added mid-stream via pipeline_->addHandler(),
    // in which case onStreamStarted() was never called for it.
    if (!dem_) {
        onStreamStarted(sampleRateHz);
        if (!dem_) return;
    }

    // Apply pending VFO offset (sentinel 1e38 = no change)
    const double pendingOff = pendingOffset_.exchange(1e38);
    if (pendingOff < 1e37) {
        currentOffsetHz_ = pendingOff;
        dem_->setOffset(pendingOff);
    }

    // Apply pending param changes. Tap counts are fixed per ChannelModem
    // instance, so a tap change rebuilds the demodulator from the full param map.
    bool rebuild = false;
    std::map<QString, double> paramsCopy;
    {
        std::lock_guard lock(paramMutex_);
        for (const auto& [name, value] : pendingParams_) {
            if (isTapsParam(name))
                rebuild = true;
            else if (!rebuild)
                applyParam(*dem_, name, value);
        }
        pendingParams_.clear();
        if (rebuild)
            paramsCopy = params_;
    }
    if (rebuild) {
        try {
            dem_ = createDemodulator(sampleRateHz, currentOffsetHz_, paramsCopy);
            LOG_CAT(LogCat::kDemodInit, LogLevel::Info,
                    std::string(modemName()) + ": rebuilt for new FIR tap counts");
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string(modemName()) + " rebuild failed: " + ex.what());
        }
    }

    const QVector<float> audio = dem_->pushBlock(iq, count);
    if (!audio.isEmpty())
        emit audioReady(audio, dem_->audioSampleRate());
}
