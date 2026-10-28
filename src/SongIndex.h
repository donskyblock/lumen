#pragma once
#include "Catalog.h"
#include "Library.h"
#include <QObject>
#include <QTimer>
#include <QVariantMap>

// A compact, ever-growing memory of every song Lumen has come across, and how songs and artists relate.
//
//  * Learns from everything: songs you play (online ones too), songs shown in searches, songs recommended
//    by extensions and the catalog, the queries you type, and which songs follow which.
//  * Feeds autoplay: candidates come from what you actually listened to and searched for, not from
//    anything that merely has a similar title.
//  * Grows quietly in the background: one artist at a time it asks the catalog for top songs and related
//    artists of the artists you care about and files them, so tomorrow's radio has more to choose from.
//
// Storage is small on purpose: one short row per recording (hash key, short strings, packed cover) and a
// capped table; see Db::idx*. Everything is local. Nothing leaves the computer except the normal catalog
// requests the passive indexer makes (switch it off with setPassive(false)).
class SongIndex : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool passive READ passive WRITE setPassive NOTIFY changed)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
public:
    SongIndex(Catalog* catalog, Library* library, QObject* parent = nullptr);

    struct Pick { Track t; double w; QString why; };

    static qint64 hashOf(const QString& title, const QString& artist);
    static qint64 hashOf(const Track& t) { return hashOf(t.title, t.artist); }

    void start();                                                       // passive indexing + first-run back-fill
    void noteSongs(const QVector<Track>& tracks, int flags = 2);        // seen / recommended
    void noteListen(const Track& t);                                    // a counted play (online ones included)
    Q_INVOKABLE void noteQuery(const QString& query);                   // debounced; from the search box
    QVector<Pick> candidates(const QVector<Track>& seeds, int n);       // database only, instant

    bool passive() const { return m_passive; }
    void setPassive(bool on);
    QVariantMap stats() const;
    void crawlNow() { crawlTick(); }                                    // for tests

signals:
    void changed();

private:
    void backfill();
    void crawlTick();
    void crawlArtist(const Db::IdxArtist& a);
    Track toTrack(const Db::IdxSong& s) const;
    void scheduleNext(bool ok);

    Catalog* m_catalog;
    Library* m_library;
    QTimer m_crawl, m_queryTimer;
    QString m_pendingQuery;
    bool m_passive = true;
    bool m_busy = false;
    int m_failStreak = 0;
    qint64 m_prevHash = 0;
    qint64 m_prevAt = 0;
    QString m_prevArtist;
};
