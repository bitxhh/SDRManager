#include "ModemRegistry.h"
#include "FmModemHandler.h"
#include "AmModemHandler.h"
#include "NfmModemHandler.h"
#include "SsbModemHandler.h"
#include "CwModemHandler.h"
#include "SamModemHandler.h"

ModemRegistry::ModemRegistry() {
    add(QStringLiteral("FM"), [](double off, QObject* p) -> ModemHandler* {
        return new FmModemHandler(off, 75e-6, 150'000.0, p);
    });
    add(QStringLiteral("NFM"), [](double off, QObject* p) -> ModemHandler* {
        return new NfmModemHandler(off, 12'500.0, 5'000.0, p);
    });
    add(QStringLiteral("AM"), [](double off, QObject* p) -> ModemHandler* {
        return new AmModemHandler(off, 5'000.0, p);
    });
    add(QStringLiteral("SAM"), [](double off, QObject* p) -> ModemHandler* {
        return new SamModemHandler(off, 5'000.0, 100.0, p);
    });
    add(QStringLiteral("USB"), [](double off, QObject* p) -> ModemHandler* {
        return new SsbModemHandler(off, +1, 2'800.0, p);
    });
    add(QStringLiteral("LSB"), [](double off, QObject* p) -> ModemHandler* {
        return new SsbModemHandler(off, -1, 2'800.0, p);
    });
    add(QStringLiteral("CW"), [](double off, QObject* p) -> ModemHandler* {
        return new CwModemHandler(off, 500.0, 700.0, p);
    });
}

ModemRegistry& ModemRegistry::instance() {
    static ModemRegistry reg;
    return reg;
}

void ModemRegistry::add(const QString& name, Factory factory) {
    factories_.insert(name, std::move(factory));
}

QStringList ModemRegistry::names() const {
    return factories_.keys();
}

ModemHandler* ModemRegistry::create(const QString& name, double offsetHz,
                                    QObject* parent) const {
    auto it = factories_.find(name);
    if (it == factories_.end()) return nullptr;
    return it.value()(offsetHz, parent);
}
