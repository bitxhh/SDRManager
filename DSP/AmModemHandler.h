#pragma once

#include "ModemHandler.h"

// ---------------------------------------------------------------------------
// AmModemHandler — AM envelope modem.
// Params: Bandwidth (1–20 kHz).
// ---------------------------------------------------------------------------
class AmModemHandler : public ModemHandler {
    Q_OBJECT

public:
    explicit AmModemHandler(double stationOffsetHz = 0.0,
                            double bandwidthHz     = 5'000.0,
                            QObject* parent        = nullptr);

    std::vector<modem::ParamDesc> paramDescriptors() const override;

protected:
    std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) override;

    void applyParam(ChannelModem& dem,
                    const QString& name, double value) override;

    const char* modemName() const override { return "AmModemHandler"; }
};
