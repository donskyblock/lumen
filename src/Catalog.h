#pragma once
#include "Db.h"
#include "Library.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <deque>
#include <functional>

// The built-in online catalog: artist pages, discographies, top songs, related
// artists and charts for any artist, with no account, key or extension.
// Metadata comes from Deezer's public API (https://developers.deezer.com/api);
// playback addresses are found at play time by the Resolver.
//
// Everything is cached in SQLite, requests are paced to stay under the API's
// quota, and every track shown is remembered so playlists/likes/history keep
// working for it after a restart.
class Catalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(QString importing READ importing NOTIFY importChanged)       // id being imported, "" when idle
    Q_PROPERTY(double importProgress READ importProgress NOTIFY importChanged)
    Q_PROPERTY(QString importLabel READ importLabel NOTIFY importChanged)
    Q_PROPERTY(QVariantMap charts READ charts NOTIFY chartsChanged)
    Q_PROPERTY(int savedRevision READ savedRevision NOTIFY savedChanged)
public:
    using Reply = std::function<void(const QJsonObject& root, const QString& error)>;
    explicit Catalog(Library* lib, QObject* parent = nullptr);

    bool online() const { return m_online; }
    QString importing() const { return m_importId; }
    double importProgress() const { return m_importProgress; }
    QString importLabel() const { return m_importLabel; }
    QVariantMap charts() const { return m_charts; }
    int savedRevision() const { return m_savedRev; }

    // ---- for QML ----
    Q_INVOKABLE int search(const QString& query);                // -> searchDone(gen, {artists, tracks, albums})
    Q_INVOKABLE void loadArtist(const QString& id);              // -> artistLoaded(id, page)
    Q_INVOKABLE void loadAlbum(const QString& id);               // -> albumLoaded(id, page)
    Q_INVOKABLE void loadCharts();
    Q_INVOKABLE void openArtistByName(const QString& name);      // -> navigateArtist(id)
    Q_INVOKABLE QStringList artistPaths(const QString& id) const;    // popular songs of the last loaded page
    Q_INVOKABLE QStringList albumPaths(const QString& id) const;
    Q_INVOKABLE bool isSavedArtist(const QString& id) const;
    Q_INVOKABLE bool isSaved(const QString& path) const { return m_lib->isSaved(path); }
    Q_INVOKABLE void importArtist(const QString& id);            // every song, all albums
    Q_INVOKABLE void importAlbum(const QString& id);
    Q_INVOKABLE void importTracks(const QStringList& paths);     // single songs (from a menu)
    Q_INVOKABLE void removeArtist(const QString& id);
    Q_INVOKABLE void removeTracks(const QStringList& paths);

    // ---- for the rest of the app ----
    void get(const QString& path, int ttlSec, Reply cb, int tries = 0);
    void getAll(const QString& path, int ttlSec, int maxItems, std::function<void(const QJsonArray&, const QString&)> cb);
    void freshPreview(const QString& id, std::function<void(const QString& url, qint64 durationMs, const QString& err)> cb);
    void resolveArtist(const QString& name, std::function<void(const std::optional<Db::CatArtist>&)> cb);
    void relatedArtists(const QString& id, std::function<void(const QVector<Db::CatArtist>&)> cb);
    void topTracks(const QString& id, std::function<void(const QVector<Track>&)> cb);
    void findTrack(const QString& artist, const QString& title, std::function<void(const std::optional<Track>&)> cb);
    void importArtistQuiet(const QString& id, std::function<void(int count)> done);   // used by the algorithm import
    Track toTrack(const QJsonObject& o, const QJsonObject& album = {}) const;
    QVariantMap trackMap(const Track& t) const;
    static QString idOf(const QJsonValue& v) { return QString::number(v.toVariant().toLongLong()); }

signals:
    void onlineChanged();
    void importChanged();
    void chartsChanged();
    void savedChanged();
    void searchDone(int gen, const QVariantMap& result);
    void artistLoaded(const QString& id, const QVariantMap& page);
    void albumLoaded(const QString& id, const QVariantMap& page);
    void loadFailed(const QString& what, const QString& id, const QString& error);
    void navigateArtist(const QString& id);
    void message(const QString& text);

private:
    struct Pending { QString path; int ttl; Reply cb; int tries; };
    void pump();
    void send(Pending p);
    void setOnline(bool on);
    QVariantMap artistMap(const QJsonObject& o) const;
    QVariantMap albumMap(const QJsonObject& o, const QString& artistName = {}) const;
    QVector<Track> remember(const QVector<Track>& t);
    void collectArtistTracks(const QString& artistId, std::function<void(const Db::CatArtist&, const QVector<Track>&, const QString&)> done,
                             std::function<void(double)> progress);
    void finishImport(const QString& what, int count);
    void setImport(const QString& id, const QString& label, double p);

    Library* m_lib;
    QNetworkAccessManager m_net;
    QString m_base;
    std::deque<Pending> m_queue;
    std::deque<qint64> m_sent;
    QTimer m_pump;
    int m_inflight = 0;
    bool m_online = true;
    int m_searchGen = 0;
    QVariantMap m_charts;
    QHash<QString, QVariantMap> m_artistPages, m_albumPages;
    QStringList m_pageOrder;
    QString m_importId, m_importLabel;
    double m_importProgress = 0;
    int m_savedRev = 0;
};
