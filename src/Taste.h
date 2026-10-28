#pragma once
#include "Catalog.h"
#include "Library.h"
#include "Playlists.h"
#include "PluginManager.h"
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

// "Your algorithm": a small, readable recommender you can tune and carry between devices.
//
//  * Signals: plays (fading with a half-life you choose), likes, skips, artists you've saved.
//  * Profile: an affinity score per artist, computed from those signals.
//  * Radio / Discover: seeds = your best artists; candidates = their songs plus songs by
//    related artists; scored by affinity, popularity, novelty, likes and skips; then
//    sampled and ordered so the same artist doesn't repeat back to back.
//  * Export: knobs + listening history + likes + playlists + saved artists in one JSON file,
//    so a new install can rebuild the same taste.
class SongIndex;
class Taste : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList paramInfo READ paramInfo NOTIFY paramsChanged)
    Q_PROPERTY(QVariantList profile READ profile NOTIFY profileChanged)          // top artists with affinity 0..1
    Q_PROPERTY(QVariantList discover READ discover NOTIFY discoverChanged)       // artists to try
    Q_PROPERTY(QString discoverReason READ discoverReason NOTIFY discoverChanged)
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)                        // what it is doing, "" when idle
    Q_PROPERTY(int signalCount READ signalCount NOTIFY profileChanged)
public:
    Taste(Library* lib, Catalog* catalog, Playlists* playlists, QObject* parent = nullptr);
    void setIndex(SongIndex* i) { m_index = i; }

    QVariantList paramInfo() const;
    QVariantList profile() const { return m_profileList; }
    QVariantList discover() const { return m_discover; }
    QString discoverReason() const { return m_discoverReason; }
    QString busy() const { return m_busy; }
    int signalCount() const { return m_signals; }
    double param(const QString& key) const { return m_params.value(key); }

    Q_INVOKABLE void setParam(const QString& key, double value);
    Q_INVOKABLE void resetParams();
    Q_INVOKABLE void startRadio(const QString& seed);       // "taste" or "artist:<catalog id>"
    Q_INVOKABLE void loadDiscover();
    void setPlugins(PluginManager* p) { m_plugins = p; }
    // Autoplay: pick `want` songs that follow on from `seeds` (most recent last), pooling your library,
    // the online catalog and every enabled extension that offers "source.recommend", then ranking
    // them with your algorithm. Emits autoplayReady with the same `ticket`.
    void autoplay(int ticket, const QStringList& seeds, const QStringList& exclude);
    Q_INVOKABLE QString exportTo(const QString& fileOrUrl);      // "" on success, otherwise a message
    Q_INVOKABLE void importFrom(const QString& fileOrUrl);       // asynchronous: importFinished() when done
    Q_INVOKABLE void importSpotifyExport(const QString& fileOrUrl);
    Q_INVOKABLE QString defaultExportName() const;

signals:
    void paramsChanged();
    void profileChanged();
    void discoverChanged();
    void busyChanged();
    void radioReady(const QString& title, const QStringList& paths);
    void radioFailed(const QString& why);
    void autoplayReady(int ticket, const QStringList& paths, const QStringList& sources);
    void importFinished(const QString& summary);
    void importFailed(const QString& why);

private:
    SongIndex* m_index = nullptr;
    struct Affinity { QString name; double score = 0; };
    QHash<QString, Affinity> computeProfile() const;
    void refreshProfile();
    void loadParams();
    void saveParams() const;
    void setBusy(const QString& s);
    QVector<QPair<QString, double>> seedArtists(int n) const;      // (name, weight)
    void resolveSeeds(const QVector<QPair<QString, double>>& names, std::function<void(const QVector<QPair<Db::CatArtist, double>>&)> cb);
    void buildRadio(const QString& title, const QVector<QPair<Db::CatArtist, double>>& seeds, bool artistRadio);
    QJsonObject trackMeta(const Track& t) const;

    Library* m_lib;
    Catalog* m_catalog;
    Playlists* m_playlists;
    PluginManager* m_plugins = nullptr;
    int m_autoJob = 0;
    QHash<QString, double> m_params;
    QHash<QString, Affinity> m_profile;
    QVariantList m_profileList, m_discover;
    QString m_discoverReason, m_busy;
    int m_signals = 0;
    QTimer m_refresh;
    int m_job = 0;
    std::shared_ptr<std::function<void()>> m_importStep;
};
