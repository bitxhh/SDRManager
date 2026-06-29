#include "ModemRegistry.h"
#include "FmModemHandler.h"
#include "AmModemHandler.h"

ModemRegistry::ModemRegistry() {
    add(QStringLiteral("FM"), [](double off, QObject* p) -> ModemHandler* {
        return new FmModemHandler(off, 75e-6, 150'000.0, p);
    });
    add(QStringLiteral("AM"), [](double off, QObject* p) -> ModemHandler* {
        return new AmModemHandler(off, 5'000.0, p);
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
