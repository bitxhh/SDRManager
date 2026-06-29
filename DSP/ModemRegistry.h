#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <functional>

class ModemHandler;
class QObject;

// ---------------------------------------------------------------------------
// ModemRegistry — factory for modem handlers.
//
// Usage:
//   auto& reg = ModemRegistry::instance();
//   QStringList modes = reg.names();          // → ["FM", "AM", ...]
//   auto* handler = reg.create("FM", offsetHz, parent);
//   auto descs = handler->paramDescriptors(); // → UI auto-builds widgets
// ---------------------------------------------------------------------------
class ModemRegistry {
public:
    using Factory = std::function<ModemHandler*(double offsetHz, QObject* parent)>;

    static ModemRegistry& instance();

    void add(const QString& name, Factory factory);
    [[nodiscard]] QStringList names() const;
    ModemHandler* create(const QString& name, double offsetHz,
                         QObject* parent = nullptr) const;

private:
    ModemRegistry();
    QMap<QString, Factory> factories_;
};
