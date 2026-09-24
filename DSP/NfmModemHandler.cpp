#include "NfmModemHandler.h"
#include "NfmModem.h"
#include "CtcssDetector.h"
#include "DcsDetector.h"

NfmModemHandler::NfmModemHandler(double stationOffsetHz,
                                 double bandwidthHz,
                                 double maxDeviationHz,
                                 QObject* parent)
    : ModemHandler(stationOffsetHz, parent)
{
    setParam(QStringLiteral("Bandwidth"), bandwidthHz);
    setParam(QStringLiteral("Deviation"), maxDeviationHz);
}

std::vector<modem::ParamDesc> NfmModemHandler::paramDescriptors() const {
    return {
        modem::SpinParam{
            QStringLiteral("Bandwidth"),
            6, 25, 12.5,
            QStringLiteral(" kHz"), 0.5, 1000.0
        },
        modem::SpinParam{
            QStringLiteral("Deviation"),
            2.5, 8, 5,
            QStringLiteral(" kHz"), 0.5, 1000.0
        },
        ctcssParam(),
        dcsParam()
    };
}

namespace {
modem::ComboParam makeCtcssParam() {
    modem::ComboParam p{QStringLiteral("CTCSS"), {}, 0};
    p.options.push_back({QStringLiteral("Off"), 0.0});
    for (double hz : CtcssDetector::kTones)
        p.options.push_back({QString::number(hz, 'f', 1) + QStringLiteral(" Hz"), hz});
    return p;
}

modem::ComboParam makeDcsParam() {
    modem::ComboParam p{QStringLiteral("DCS"), {}, 0};
    p.options.push_back({QStringLiteral("Off"), 0.0});
    for (int code : DcsDetector::kCodes)
        p.options.push_back({QString("%1").arg(code, 3, 8, QChar('0')), double(code)});
    return p;
}
} // namespace

modem::ComboParam NfmModemHandler::ctcssParam() {
    static const modem::ComboParam p = makeCtcssParam();
    return p;
}

modem::ComboParam NfmModemHandler::dcsParam() {
    static const modem::ComboParam p = makeDcsParam();
    return p;
}

void NfmModemHandler::buildAudioChain(ChannelModem& dem,
                                      const std::map<QString, double>& /*params*/) {
    // DCS first: CtcssDetector's 300 Hz highpass would remove the DCS band.
    auto dcs = std::make_unique<DcsDetector>();
    dcs_ = dcs.get();
    dem.addAudioProcessor(std::move(dcs));

    auto det = std::make_unique<CtcssDetector>();
    ctcss_ = det.get();
    dem.addAudioProcessor(std::move(det));
}

void NfmModemHandler::processBlock(const float* iq, int count, double sampleRateHz) {
    ModemHandler::processBlock(iq, count, sampleRateHz);
    if (!dem_) { ctcss_ = nullptr; dcs_ = nullptr; }
    const double tone = ctcss_ ? ctcss_->detectedToneHz() : 0.0;
    if (tone != lastTone_) {
        lastTone_ = tone;
        emit ctcssToneChanged(tone);
    }
    const int code = dcs_ ? dcs_->detectedCode() : 0;
    if (code != lastDcs_) {
        lastDcs_ = code;
        emit dcsCodeChanged(code);
    }
}

void NfmModemHandler::onStreamStopped() {
    ModemHandler::onStreamStopped();
    ctcss_ = nullptr;
    if (lastTone_ != 0.0) {
        lastTone_ = 0.0;
        emit ctcssToneChanged(0.0);
    }
    dcs_ = nullptr;
    if (lastDcs_ != 0) {
        lastDcs_ = 0;
        emit dcsCodeChanged(0);
    }
}

std::unique_ptr<ChannelModem>
NfmModemHandler::createDemodulator(double sampleRateHz, double offsetHz,
                                   const std::map<QString, double>& params) {
    auto get = [&](const QString& k, double def) {
        auto it = params.find(k);
        return it != params.end() ? it->second : def;
    };
    const double bw  = get(QStringLiteral("Bandwidth"), 12'500.0);
    const double dev = get(QStringLiteral("Deviation"),  5'000.0);
    return std::make_unique<NfmModem>(sampleRateHz, offsetHz, bw, dev,
        tapsParam(params, kFir1TapsKey, kDefaultFir1Taps),
        tapsParam(params, kFir2TapsKey, kDefaultFir2Taps));
}

void NfmModemHandler::applyParam(ChannelModem& dem,
                                 const QString& name, double value) {
    if (name == QLatin1String("Bandwidth"))
        static_cast<NfmModem&>(dem).setBandwidth(value);
    else if (name == QLatin1String("Deviation"))
        static_cast<NfmModem&>(dem).setDeviation(value);
}
