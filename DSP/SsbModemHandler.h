#pragma once

#include "ModemHandler.h"

// ---------------------------------------------------------------------------
// SsbModemHandler — single-sideband modem (USB or LSB).
//
// One parameterized handler serves both sidebands: the registry registers it
// twice ("USB" with sideband=+1, "LSB" with sideband=-1). The sideband is a
// construction argument, NOT a user param — the only exposed param is
// Bandwidth (1–4 kHz audio passband).
// ---------------------------------------------------------------------------
class SsbModemHandler : public ModemHandler {
    Q_OBJECT

public:
    // sideband: +1 = USB, -1 = LSB.
    explicit SsbModemHandler(double stationOffsetHz = 0.0,
                             int    sideband        = +1,
                             double bandwidthHz     = 2'800.0,
                             QObject* parent        = nullptr);

    std::vector<modem::ParamDesc> paramDescriptors() const override;

protected:
    std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) override;

    void applyParam(ChannelModem& dem,
                    const QString& name, double value) override;

    const char* modemName() const override {
        return sideband_ > 0 ? "UsbModemHandler" : "LsbModemHandler";
    }

private:
    int sideband_;   // +1 USB, -1 LSB
};
