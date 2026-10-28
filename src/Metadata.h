#pragma once
#include "Catalog.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include <memory>

// Song metadata lookup that is separate from the catalog browser and needs no account or key.
//
// Given a messy title/artist (typically from a YouTube or SoundCloud upload) it finds the real
// recording and returns clean fields: title, artist, album, year, genre, ISRC, cover and length.
// Providers are tried in order until one is confident:
//
//   deezer       public API, ~50 requests / 5 s per IP; goes through Catalog (shared pacing + cache)
//   itunes       Apple's public search API, no key; documented soft limit is around 20 calls / minute
//   musicbrainz  open database, no key; asks for at most 1 request / second and a contact User-Agent
//
// Every answer is cached on disk (SQLite) and remembered for the session, and each provider is paced
// on its own, so the combined free allowance is far higher than any one of them, and repeat lookups
// cost nothing. Order and addresses can be overridden for self-hosted mirrors:
//   LUMEN_META_PROVIDERS=deezer,itunes,musicbrainz
//   LUMEN_ITUNES_URL, LUMEN_MUSICBRAINZ_URL, LUMEN_CONTACT (shown to MusicBrainz in the User-Agent)
struct SongMeta {
    bool ok = false;
    double confidence = 0;              // 0..1, how sure we are it is the same recording
    QString title, artist, album, genre, isrc, cover, source;
    QString artistId, albumId, catalogId;   // Deezer ids when the match came from Deezer
    int year = 0;
    qint64 durationMs = 0;
    QVariantMap toMap() const;
};

class Metadata : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(const SongMeta&)>;
    explicit Metadata(Catalog* catalog, QObject* parent = nullptr);

    void lookup(const QString& title, const QString& artist, qint64 durationMs, Done cb);
    // QML / scripting entry point: answer arrives through found().
    Q_INVOKABLE void request(const QString& title, const QString& artist, qint64 durationMs = 0);
    Q_INVOKABLE QStringList providers() const { return m_providers; }

signals:
    void found(const QString& title, const QString& artist, const QVariantMap& meta);

private:
    struct Job {
        QString title, artist, key;
        qint64 durationMs = 0;
        Done cb;
        SongMeta best;
        int provider = 0;
    };
    void next(std::shared_ptr<Job> job);
    void finish(std::shared_ptr<Job> job, SongMeta m);
    void deezer(std::shared_ptr<Job> job, std::function<void(SongMeta)> done);
    void itunes(std::shared_ptr<Job> job, std::function<void(SongMeta)> done);
    void musicbrainz(std::shared_ptr<Job> job, std::function<void(SongMeta)> done);
    void fetch(const QString& lane, int gapMs, const QUrl& url, int ttlSec, std::function<void(const QJsonObject&)> cb);

    Catalog* m_catalog;
    QNetworkAccessManager m_net;
    QStringList m_providers;
    QString m_itunes, m_mb, m_contact;
    QHash<QString, SongMeta> m_memo;
    QHash<QString, qint64> m_laneNext;      // lane -> earliest time (ms since epoch) the next request may leave
};
