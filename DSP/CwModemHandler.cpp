#include "CwModemHandler.h"
#include "CwModem.h"

CwModemHandler::CwModemHandler(double stationOffsetHz,
                               double bandwidthHz,
                               double pitchHz,
                               QObject* parent)
    : ModemHandler(stationOffsetHz, parent)
{
    setParam(QStringLiteral("Bandwidth"), bandwidthHz);
    setParam(QStringLiteral("Pitch"), pitchHz);
}

std::vector<modem::ParamDesc> CwModemHandler::paramDescriptors() const {
    return {
        modem::SpinParam{
            QStringLiteral("Bandwidth"),
            100, 1000, 500,
            QStringLiteral(" Hz"), 50, 1.0
        },
        modem::SpinParam{
            QStringLiteral("Pitch"),
            300, 1000, 700,
            QStringLiteral(" Hz"), 10, 1.0
        }
    };
}

std::unique_ptr<ChannelModem>
CwModemHandler::createDemodulator(double sampleRateHz, double offsetHz,
                                  const std::map<QString, double>& params) {
    auto get = [&](const QString& k, double def) {
        auto it = params.find(k);
        return it != params.end() ? it->second : def;
    };
    const double bw    = get(QStringLiteral("Bandwidth"), 500.0);
    const double pitch = get(QStringLiteral("Pitch"),     700.0);
    return std::make_unique<CwModem>(sampleRateHz, offsetHz, bw, pitch,
        tapsParam(params, kFir1TapsKey, kDefaultFir1Taps),
        tapsParam(params, kChanTapsKey, kDefaultChanTaps));
}

void CwModemHandler::applyParam(ChannelModem& dem,
                                const QString& name, double value) {
    if (name == QLatin1String("Bandwidth"))
        static_cast<CwModem&>(dem).setBandwidth(value);
    else if (name == QLatin1String("Pitch"))
        static_cast<CwModem&>(dem).setPitch(value);
}
