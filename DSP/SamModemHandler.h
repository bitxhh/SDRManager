#pragma once

#include "ModemHandler.h"

// ---------------------------------------------------------------------------
// SamModemHandler — synchronous AM (SAM / DSB) modem.
// Params: Bandwidth (1–20 kHz), PLL BW (10–500 Hz carrier loop bandwidth).
// ---------------------------------------------------------------------------
class SamModemHandler : public ModemHandler {
    Q_OBJECT

public:
    explicit SamModemHandler(double stationOffsetHz = 0.0,
                             double bandwidthHz     = 5'000.0,
                             double pllBwHz         = 100.0,
                             QObject* parent        = nullptr);

    std::vector<modem::ParamDesc> paramDescriptors() const override;

protected:
    std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) override;

    void applyParam(ChannelModem& dem,
                    const QString& name, double value) override;

    const char* modemName() const override { return "SamModemHandler"; }
};
