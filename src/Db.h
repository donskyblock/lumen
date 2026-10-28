#pragma once
#include "Track.h"
#include <QHash>
#include <QPair>
#include <QString>
#include <optional>

struct sqlite3;

// Single SQLite file in ~/.local/share/lumen/lumen.db (override with
// LUMEN_DATA_DIR). Holds library folders, the track cache (so start-up is
// instant and rescans only probe changed files), cached lyrics and a small
// key/value table for UI + plugin + session state. Main thread only.
class Db {
public:
    static Db& get();
    bool ok() const { return m_db != nullptr; }
    QString dataDir() const { return m_dir; }

    // key/value
    QString kv(const QString& key, const QString& def = {});
    void setKv(const QString& key, const QString& value);

    // folders
    QStringList folders();
    void addFolder(const QString& path);
    void removeFolder(const QString& path);

    // tracks
    QVector<Track> allTracks();
    QHash<QString, QPair<qint64, qint64>> signatures();    // path -> (mtime, size)
    void upsertTracks(const QVector<Track>& tracks);
    void removeTracks(const QStringList& paths);

    // lyrics cache. provider empty + empty text == "looked, found nothing"
    struct LyricsRec { QString provider; bool synced = false; QString text; qint64 fetched = 0; };
    std::optional<LyricsRec> lyrics(const QString& key);
    void putLyrics(const QString& key, const LyricsRec& rec);
    void clearLyrics(const QString& key);

    // playlists (items keep a snapshot of the metadata so missing files still show)
    struct PlaylistRow { qint64 id = 0; QString name, special; qint64 created = 0; };
    struct PlItem { QString path, title, artist, album; qint64 durationMs = 0; };
    QVector<PlaylistRow> playlists();
    qint64 createPlaylist(const QString& name, const QString& special = {});
    void renamePlaylist(qint64 id, const QString& name);
    void deletePlaylist(qint64 id);
    QVector<PlItem> playlistItems(qint64 id);
    void setPlaylistItems(qint64 id, const QVector<PlItem>& items);

    // listening history
    struct PlayRec { QString path; qint64 at = 0; qint64 ms = 0; };
    void recordPlay(const QString& path, qint64 at, qint64 ms);
    QVector<PlayRec> plays(qint64 sinceEpoch);

    // online catalog: every track Lumen has shown is remembered (saved=false); "Add to library" flips saved.
    struct CatArtist { QString id, name, picture; qint64 fans = 0; bool saved = false; };
    void upsertCatTracks(const QVector<Track>& tracks, bool saved);
    std::optional<Track> catTrack(const QString& id);
    QVector<Track> catTracks(bool savedOnly);
    void unsaveCatTracks(const QStringList& ids);
    void upsertCatArtist(const CatArtist& a, bool saved);
    std::optional<CatArtist> catArtist(const QString& id);
    std::optional<CatArtist> catArtistByName(const QString& name);
    QVector<CatArtist> savedCatArtists();
    void unsaveCatArtist(const QString& id);

    // small HTTP response cache for the catalog
    std::optional<QByteArray> cacheGet(const QString& url, qint64 maxAgeSec);
    void cachePut(const QString& url, const QByteArray& body);
    void cachePrune(qint64 olderThanSec);

    // "not for me" signals (skipped early) feed the taste algorithm
    void recordSkip(const QString& path, qint64 at);
    QVector<PlayRec> skips(qint64 sinceEpoch);

    // ---- the song index: a compact, ever-growing memory of every song we have seen -------------------
    // Songs are keyed by a 63-bit hash of (title, artist, edition), so the same recording found through
    // the catalog, YouTube and a search is one row. Rows are tiny and the table is capped (idxPrune).
    struct IdxSong {
        qint64 h = 0;
        QString path, title, artist, key, album, artistId, cover;     // key = primaryArtistKey(artist)
        qint64 durationMs = 0;
        int rank = 0, flags = 0, seen = 0;                             // flags: 1 listened, 2 seen/recommended, 4 searched
    };
    struct IdxArtist { QString key, name, cid; double w = 0; qint64 crawled = 0; };
    struct IdxCounts { qint64 songs = 0, artists = 0, edges = 0, searches = 0, crawled = 0; };
    void idxUpsertSongs(const QVector<IdxSong>& songs);
    QVector<IdxSong> idxSongsByArtist(const QString& key, int limit);
    QVector<IdxSong> idxSongsByHash(const QVector<qint64>& hashes);
    void idxTouchArtist(const QString& key, const QString& name, const QString& cid, double addWeight);
    std::optional<IdxArtist> idxArtist(const QString& key);
    QVector<IdxArtist> idxArtistsToCrawl(int n, qint64 notCrawledSince);
    QVector<IdxArtist> idxTopArtists(int n);
    void idxMarkCrawled(const QString& key, qint64 at);
    void idxAddEdge(const QString& a, const QString& b, double w);          // artist <-> artist, both directions
    QVector<QPair<QString, double>> idxRelated(const QString& key, int n);
    void idxAddPair(qint64 from, qint64 to, double w);                      // song played right after song
    QVector<QPair<qint64, double>> idxPairsFrom(qint64 h, int n);
    void idxNoteSearch(const QString& query, const QString& artistKey, qint64 at);
    QVector<QPair<QString, QString>> idxRecentSearches(int n);              // (query, artist key or "")
    IdxCounts idxCounts();
    void idxPrune(qint64 maxSongs);

    void begin();
    void commit();

private:
    Db();
    ~Db();
    void exec(const char* sql);
    void migrate();
    sqlite3* m_db = nullptr;
    QString m_dir;
};
