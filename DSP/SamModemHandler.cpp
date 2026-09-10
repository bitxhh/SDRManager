#include "SamModemHandler.h"
#include "SamModem.h"

SamModemHandler::SamModemHandler(double stationOffsetHz,
                                 double bandwidthHz,
                                 double pllBwHz,
                                 QObject* parent)
    : ModemHandler(stationOffsetHz, parent)
{
    setParam(QStringLiteral("Bandwidth"), bandwidthHz);
    setParam(QStringLiteral("PLL BW"), pllBwHz);
}

std::vector<modem::ParamDesc> SamModemHandler::paramDescriptors() const {
    return {
        modem::SpinParam{
            QStringLiteral("Bandwidth"),
            1, 20, 5,
            QStringLiteral(" kHz"), 1, 1000.0
        },
        modem::SpinParam{
            QStringLiteral("PLL BW"),
            10, 500, 100,
            QStringLiteral(" Hz"), 10, 1.0
        }
    };
}

std::unique_ptr<ChannelModem>
SamModemHandler::createDemodulator(double sampleRateHz, double offsetHz,
                                   const std::map<QString, double>& params) {
    auto get = [&](const QString& k, double def) {
        auto it = params.find(k);
        return it != params.end() ? it->second : def;
    };
    const double bw     = get(QStringLiteral("Bandwidth"), 5'000.0);
    const double pllBw  = get(QStringLiteral("PLL BW"),      100.0);
    return std::make_unique<SamModem>(sampleRateHz, offsetHz, bw, pllBw);
}

void SamModemHandler::applyParam(ChannelModem& dem,
                                 const QString& name, double value) {
    if (name == QLatin1String("Bandwidth"))
        static_cast<SamModem&>(dem).setBandwidth(value);
    else if (name == QLatin1String("PLL BW"))
        static_cast<SamModem&>(dem).setPllBandwidth(value);
}
