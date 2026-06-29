#pragma once

#include "ModemTypes.h"   // modem::ParamDesc
#include "IModulator.h"   // complete type — required for inline makeModulator()

#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// IModem — contract of a modulation scheme. Implemented by a per-modem handler.
//
// A "modem" bundles the RX (demodulator) and the future TX (modulator) of one
// scheme. Third parties implement this single contract to add a custom modem.
// ---------------------------------------------------------------------------
class IModem {
public:
    virtual ~IModem() = default;

    // Scheme name for the registry/UI, e.g. "FM", "AM".
    virtual const char* modemName() const = 0;

    // Parameter metadata for UI auto-build.
    virtual std::vector<modem::ParamDesc> paramDescriptors() const { return {}; }

    // TX side. By default a modem has no modulator.
    virtual std::unique_ptr<IModulator> makeModulator() { return nullptr; }
};
