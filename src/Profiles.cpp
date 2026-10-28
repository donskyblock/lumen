#include "Profiles.h"
#include "Db.h"
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>

namespace {
QJsonObject make(const char* id, const char* name, const char* scheme, bool fixed, int hue, int chroma, int radius,
                 int density, int backdrop, bool motion, int font = 100, int grid = 1, bool wavy = true)
{
    return {{"id", id}, {"name", name}, {"scheme", scheme}, {"accent", fixed ? "fixed" : "cover"}, {"hue", hue},
            {"chroma", chroma}, {"radius", radius}, {"density", density}, {"backdrop", backdrop}, {"motion", motion},
            {"fontScale", font}, {"grid", grid}, {"wavy", wavy}, {"homeHidden", QJsonArray()}};
}
}

QJsonObject Profiles::preset(const QString& id)
{
    if (id == "midnight") return make("midnight", "Midnight", "amoled", true, 222, 55, 1, 1, 35, true);
    if (id == "paper")    return make("paper", "Paper", "light", false, 208, 55, 2, 1, 30, true);
    if (id == "neon")     return make("neon", "Neon", "dark", true, 315, 92, 0, 1, 85, true);
    if (id == "cozy")     return make("cozy", "Cozy", "dark", true, 28, 55, 2, 1, 70, true, 105);
    if (id == "focus")    return make("focus", "Focus", "amoled", true, 200, 8, 0, 0, 0, false, 100, 1, false);
    return make("default", "Default", "dark", false, 208, 60, 1, 1, 60, true);
}

QVariantList Profiles::presets() const
{
    QVariantList out;
    for (const char* id : {"default", "midnight", "paper", "neon", "cozy", "focus"}) {
        const QJsonObject o = preset(id);
        out << QVariantMap{{"id", o["id"].toString()}, {"name", o["name"].toString()}};
    }
    return out;
}

Profiles::Profiles(QObject* parent) : QObject(parent)
{
    m_profiles = QJsonDocument::fromJson(Db::get().kv("profiles").toUtf8()).array();
    if (m_profiles.isEmpty()) m_profiles.append(preset("default"));
    m_active = Db::get().kv("profile.active", m_profiles.first().toObject()["id"].toString());
    bool found = false;
    for (const QJsonValue& v : std::as_const(m_profiles)) found = found || v.toObject()["id"].toString() == m_active;
    if (!found) m_active = m_profiles.first().toObject()["id"].toString();
}

void Profiles::save()
{
    Db::get().setKv("profiles", QString::fromUtf8(QJsonDocument(m_profiles).toJson(QJsonDocument::Compact)));
    Db::get().setKv("profile.active", m_active);
    emit changed();
}

QVariantList Profiles::list() const
{
    QVariantList out;
    for (const QJsonValue& v : m_profiles) out << v.toObject().toVariantMap();
    return out;
}

QVariantMap Profiles::active() const
{
    for (const QJsonValue& v : m_profiles)
        if (v.toObject()["id"].toString() == m_active) return v.toObject().toVariantMap();
    return {};
}

void Profiles::select(const QString& id)
{
    if (id == m_active) return;
    for (const QJsonValue& v : std::as_const(m_profiles))
        if (v.toObject()["id"].toString() == id) { m_active = id; save(); return; }
}

QString Profiles::create(const QString& name, const QString& presetId)
{
    QJsonObject o = preset(presetId);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    o["id"] = id;
    o["name"] = name.trimmed().isEmpty() ? QStringLiteral("New profile") : name.trimmed();
    m_profiles.append(o);
    m_active = id;
    save();
    return id;
}

QString Profiles::duplicate(const QString& id)
{
    for (const QJsonValue& v : std::as_const(m_profiles)) {
        QJsonObject o = v.toObject();
        if (o["id"].toString() != id) continue;
        const QString nid = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        o["id"] = nid;
        o["name"] = o["name"].toString() + QStringLiteral(" copy");
        m_profiles.append(o);
        m_active = nid;
        save();
        return nid;
    }
    return {};
}

void Profiles::remove(const QString& id)
{
    if (m_profiles.size() <= 1) return;
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].toObject()["id"].toString() != id) continue;
        m_profiles.removeAt(i);
        if (m_active == id) m_active = m_profiles.first().toObject()["id"].toString();
        save();
        return;
    }
}

void Profiles::set(const QString& key, const QVariant& value)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        QJsonObject o = m_profiles[i].toObject();
        if (o["id"].toString() != m_active) continue;
        if (key == "name") { const QString n = value.toString().trimmed(); if (n.isEmpty()) return; o[key] = n; }
        else if (key == "scheme" || key == "accent") o[key] = value.toString();
        else if (key == "motion" || key == "wavy") o[key] = value.toBool();
        else o[key] = std::clamp(value.toInt(), 0, key == "hue" ? 359 : (key == "fontScale" ? 125 : 100));
        if (key == "fontScale") o[key] = std::clamp(value.toInt(), 85, 125);
        m_profiles[i] = o;
        save();
        return;
    }
}

void Profiles::toggleHomeSection(const QString& section)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        QJsonObject o = m_profiles[i].toObject();
        if (o["id"].toString() != m_active) continue;
        QJsonArray hidden = o["homeHidden"].toArray(), next;
        bool had = false;
        for (const QJsonValue& v : hidden) { if (v.toString() == section) had = true; else next.append(v); }
        if (!had) next.append(section);
        o["homeHidden"] = next;
        m_profiles[i] = o;
        save();
        return;
    }
}
