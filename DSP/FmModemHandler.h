#pragma once

#include "ModemHandler.h"

// ---------------------------------------------------------------------------
// FmModemHandler — WBFM modem.
// Params: Bandwidth (50–250 kHz), De-emphasis (50/75 µs).
// ---------------------------------------------------------------------------
class FmModemHandler : public ModemHandler {
    Q_OBJECT

public:
    explicit FmModemHandler(double stationOffsetHz = 0.0,
                            double deemphTauSec    = 75e-6,
                            double bandwidthHz     = 100'000.0,
                            QObject* parent        = nullptr);

    std::vector<modem::ParamDesc> paramDescriptors() const override;

protected:
    std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) override;

    void applyParam(ChannelModem& dem,
                    const QString& name, double value) override;

    const char* modemName() const override { return "FmModemHandler"; }
};
