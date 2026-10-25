#pragma once
#include "Db.h"
#include <QObject>
#include <QVariant>

// Tiny persistent key/value store for QML (window size, last page, panel state).
class UiState : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE int intValue(const QString& key, int def) const { return Db::get().kv("ui." + key, QString::number(def)).toInt(); }
    Q_INVOKABLE bool boolValue(const QString& key, bool def) const { return Db::get().kv("ui." + key, def ? "1" : "0") == "1"; }
    Q_INVOKABLE void set(const QString& key, const QVariant& v) { Db::get().setKv("ui." + key, v.typeId() == QMetaType::Bool ? (v.toBool() ? "1" : "0") : v.toString()); }
};
