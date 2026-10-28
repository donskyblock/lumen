#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>

// Profiles = named looks ("vibes"): colour scheme, accent, shapes, density,
// motion, home layout. One is active at a time; switching is instant.
class Profiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(QVariantMap active READ active NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
public:
    explicit Profiles(QObject* parent = nullptr);
    QVariantList list() const;
    QVariantMap active() const;
    QVariantList presets() const;

    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE QString create(const QString& name, const QString& presetId);
    Q_INVOKABLE QString duplicate(const QString& id);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void set(const QString& key, const QVariant& value);   // edits the active profile
    Q_INVOKABLE void toggleHomeSection(const QString& section);
    void emitActive() { emit changed(); }

signals:
    void changed();

private:
    static QJsonObject preset(const QString& id);
    void save();
    QJsonArray m_profiles;
    QString m_active;
};
