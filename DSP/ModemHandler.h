#pragma once

#include "../Core/IPipelineHandler.h"
#include "IModem.h"
#include "ChannelModem.h"
#include "ModemTypes.h"

#include <QObject>
#include <QVector>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// ModemHandler — common IPipelineHandler + IModem wrapper for all modems.
//
// A "modem" is both:
//   IPipelineHandler — RX runtime (owns a ChannelModem, drives blocks);
//   IModem           — scheme identity (modemName, paramDescriptors, makeModulator).
//
// Manages:  generic named parameters (thread-safe), lazy-init,
//           audioReady signal.
//
// Subclasses implement:
//   paramDescriptors()  — UI metadata (spin/combo definitions)
//   createDemodulator() — build concrete ChannelModem from param map
//   applyParam()        — live-update a running ChannelModem
// ---------------------------------------------------------------------------
class ModemHandler : public QObject, public IPipelineHandler, public IModem {
    Q_OBJECT

public:
    // FIR tap-count param keys. Changing one rebuilds the demodulator.
    static constexpr const char* kFir1TapsKey = "FIR1 taps";
    static constexpr const char* kFir2TapsKey = "FIR2 taps";     // FM/NFM/AM/SAM
    static constexpr const char* kChanTapsKey = "Channel taps";  // SSB/CW

    // Parameter descriptors for UI auto-build (IModem).
    std::vector<modem::ParamDesc> paramDescriptors() const override { return {}; }

    // Set parameter by name. Thread-safe (UI thread → worker thread).
    void setParam(const QString& name, double value);

    // Read current parameter value. Thread-safe.
    [[nodiscard]] double param(const QString& name) const;

    // Convenience wrappers for common parameters.
    void setBandwidth(double hz) { setParam(QStringLiteral("Bandwidth"), hz); }

    // NCO offset — universal, not a "parameter".
    void setOffset(double hz);

    // IPipelineHandler
    void processBlock(const float* iq, int count, double sampleRateHz) override;
    void onStreamStarted(double sampleRateHz) override;
    void onStreamStopped() override;

signals:
    void audioReady(QVector<float> samples, double sampleRateHz);

protected:
    explicit ModemHandler(double stationOffsetHz, QObject* parent = nullptr);

    // Subclass creates its concrete demodulator using the param snapshot.
    virtual std::unique_ptr<ChannelModem>
    createDemodulator(double sampleRateHz, double offsetHz,
                      const std::map<QString, double>& params) = 0;

    // Apply a single param to a running demodulator.
    // Override to handle live parameter updates.
    virtual void applyParam(ChannelModem& dem,
                            const QString& name, double value) {}

    // Add post-demod IAudioProcessor stages to a freshly built demodulator.
    // Called on every (re)build; afterwards the full param snapshot is
    // offered to the chain. Params a stage accepts never reach applyParam().
    virtual void buildAudioChain(ChannelModem& dem,
                                 const std::map<QString, double>& params) {}

    // Scheme name (IModem). Also used in log strings.
    const char* modemName() const override = 0;

    // Read a FIR tap-count param: clamped to [kMinFirTaps, kMaxFirTaps], forced odd.
    static int tapsParam(const std::map<QString, double>& params,
                         const QString& key, int def);

    double stationOffsetHz_;
    std::unique_ptr<ChannelModem> dem_;

private:
    // Tap-count params ("... taps") can't be applied live — they rebuild dem_.
    static bool isTapsParam(const QString& name);

    // createDemodulator() + buildAudioChain() + param snapshot → common
    // params (noise blanker) and audio chain.
    std::unique_ptr<ChannelModem> makeDemodulator(double sampleRateHz,
                                                  const std::map<QString, double>& params);

    double currentOffsetHz_;   // last applied NCO offset (worker thread)
    mutable std::mutex paramMutex_;
    std::map<QString, double> params_;
    std::vector<std::pair<QString, double>> pendingParams_;
    std::atomic<double> pendingOffset_{1e38};  // 1e38 = sentinel "no update"
};
