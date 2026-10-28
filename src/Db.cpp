#include "Db.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include "TextUtil.h"
#include <QDebug>
#include <sqlite3.h>

namespace {
struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
    ~Stmt() { sqlite3_finalize(s); }
    void text(int i, const QString& v) { const QByteArray b = v.toUtf8(); sqlite3_bind_text(s, i, b.constData(), b.size(), SQLITE_TRANSIENT); }
    void i64(int i, qint64 v) { sqlite3_bind_int64(s, i, v); }
    void dbl(int i, double v) { sqlite3_bind_double(s, i, v); }
    bool row() { return sqlite3_step(s) == SQLITE_ROW; }
    void run() { sqlite3_step(s); }
    QString str(int c) const { return QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, c))); }
    qint64 num(int c) const { return sqlite3_column_int64(s, c); }
    double real(int c) const { return sqlite3_column_double(s, c); }
};
}

Db& Db::get() { static Db db; return db; }

Db::Db()
{
    m_dir = qEnvironmentVariable("LUMEN_DATA_DIR");
    if (m_dir.isEmpty()) m_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(m_dir);
    if (sqlite3_open(QDir(m_dir).filePath("lumen.db").toUtf8().constData(), &m_db) != SQLITE_OK) {
        qWarning() << "Cannot open database in" << m_dir;
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }
    exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA temp_store=MEMORY;");
    exec("CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v TEXT);"
         "CREATE TABLE IF NOT EXISTS folders(path TEXT PRIMARY KEY, added INTEGER);"
         "CREATE TABLE IF NOT EXISTS tracks(path TEXT PRIMARY KEY, mtime INTEGER, size INTEGER,"
         " title TEXT, artist TEXT, albumArtist TEXT, album TEXT, codec TEXT, trackNo INTEGER, discNo INTEGER,"
         " year INTEGER, sampleRate INTEGER, channels INTEGER, bitDepth INTEGER, bitrate INTEGER,"
         " durationMs INTEGER, lossless INTEGER);"
         "CREATE TABLE IF NOT EXISTS lyrics(k TEXT PRIMARY KEY, provider TEXT, synced INTEGER, body TEXT, fetched INTEGER);"
         "CREATE TABLE IF NOT EXISTS playlists(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, special TEXT, created INTEGER);"
         "CREATE TABLE IF NOT EXISTS playlist_items(playlist INTEGER, pos INTEGER, path TEXT, title TEXT, artist TEXT, album TEXT, durationMs INTEGER,"
         " PRIMARY KEY(playlist, pos));"
         "CREATE TABLE IF NOT EXISTS plays(path TEXT, at INTEGER, ms INTEGER);"
         "CREATE INDEX IF NOT EXISTS plays_at ON plays(at);"
         "CREATE TABLE IF NOT EXISTS cat_tracks(id TEXT PRIMARY KEY, artist_id TEXT, artist TEXT, title TEXT, album TEXT, album_id TEXT,"
         " album_artist TEXT, cover TEXT, duration_ms INTEGER, year INTEGER, track_no INTEGER, disc_no INTEGER, rank INTEGER,"
         " saved INTEGER DEFAULT 0, added INTEGER DEFAULT 0);"
         "CREATE INDEX IF NOT EXISTS cat_tracks_saved ON cat_tracks(saved);"
         "CREATE TABLE IF NOT EXISTS cat_artists(id TEXT PRIMARY KEY, name TEXT, key TEXT, picture TEXT, fans INTEGER, saved INTEGER DEFAULT 0, added INTEGER DEFAULT 0);"
         "CREATE INDEX IF NOT EXISTS cat_artists_key ON cat_artists(key);"
         "CREATE TABLE IF NOT EXISTS http_cache(url TEXT PRIMARY KEY, body BLOB, fetched INTEGER);"
         "CREATE TABLE IF NOT EXISTS skips(path TEXT, at INTEGER);"
         // the song index (see Db.h): compact columns, integer keys, WITHOUT ROWID where there is a natural key
         "CREATE TABLE IF NOT EXISTS idx_songs(h INTEGER PRIMARY KEY, p TEXT, t TEXT, a TEXT, k TEXT, al TEXT, aid TEXT, c TEXT,"
         " d INTEGER, r INTEGER, n INTEGER DEFAULT 1, f INTEGER DEFAULT 0, l INTEGER);"
         "CREATE INDEX IF NOT EXISTS idx_songs_k ON idx_songs(k);"
         "CREATE TABLE IF NOT EXISTS idx_artists(k TEXT PRIMARY KEY, name TEXT, cid TEXT, w REAL DEFAULT 0, cr INTEGER DEFAULT 0) WITHOUT ROWID;"
         "CREATE INDEX IF NOT EXISTS idx_artists_w ON idx_artists(w);"
         "CREATE TABLE IF NOT EXISTS idx_edges(a TEXT, b TEXT, w REAL, PRIMARY KEY(a,b)) WITHOUT ROWID;"
         "CREATE TABLE IF NOT EXISTS idx_pairs(a INTEGER, b INTEGER, w REAL, PRIMARY KEY(a,b)) WITHOUT ROWID;"
         "CREATE TABLE IF NOT EXISTS idx_searches(q TEXT PRIMARY KEY, n INTEGER, at INTEGER, k TEXT) WITHOUT ROWID;");
    migrate();
}

void Db::migrate()
{
    // v1 -> v2: genre + "added" columns. Forcing mtime=0 makes the next scan re-read tags once.
    bool hasGenre = false;
    {
        Stmt s(m_db, "PRAGMA table_info(tracks)");
        while (s.row()) if (s.str(1) == QLatin1String("genre")) hasGenre = true;
    }
    if (!hasGenre) {
        exec("ALTER TABLE tracks ADD COLUMN genre TEXT DEFAULT ''");
        exec("ALTER TABLE tracks ADD COLUMN added INTEGER DEFAULT 0");
        exec("UPDATE tracks SET added = mtime WHERE added = 0");
        exec("UPDATE tracks SET mtime = 0");
    }
}

Db::~Db() { if (m_db) sqlite3_close(m_db); }
void Db::exec(const char* sql) { if (m_db) sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr); }
void Db::begin() { exec("BEGIN"); }
void Db::commit() { exec("COMMIT"); }

QString Db::kv(const QString& key, const QString& def)
{
    if (!m_db) return def;
    Stmt s(m_db, "SELECT v FROM kv WHERE k=?"); s.text(1, key);
    return s.row() ? s.str(0) : def;
}

void Db::setKv(const QString& key, const QString& value)
{
    if (!m_db) return;
    Stmt s(m_db, "INSERT INTO kv(k,v) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET v=excluded.v");
    s.text(1, key); s.text(2, value); s.run();
}

QStringList Db::folders()
{
    QStringList out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT path FROM folders ORDER BY added, path");
    while (s.row()) out << s.str(0);
    return out;
}

void Db::addFolder(const QString& path)
{
    if (!m_db) return;
    Stmt s(m_db, "INSERT OR IGNORE INTO folders(path, added) VALUES(?,?)");
    s.text(1, path); s.i64(2, QDateTime::currentSecsSinceEpoch()); s.run();
}

void Db::removeFolder(const QString& path)
{
    if (!m_db) return;
    Stmt s(m_db, "DELETE FROM folders WHERE path=?"); s.text(1, path); s.run();
}

QVector<Track> Db::allTracks()
{
    QVector<Track> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT path,mtime,size,title,artist,albumArtist,album,codec,trackNo,discNo,year,sampleRate,channels,bitDepth,bitrate,durationMs,lossless,genre,added FROM tracks");
    while (s.row()) {
        Track t;
        t.path = s.str(0); t.mtime = s.num(1); t.size = s.num(2);
        t.title = s.str(3); t.artist = s.str(4); t.albumArtist = s.str(5); t.album = s.str(6); t.codec = s.str(7);
        t.trackNo = int(s.num(8)); t.discNo = int(s.num(9)); t.year = int(s.num(10));
        t.sampleRate = int(s.num(11)); t.channels = int(s.num(12)); t.bitDepth = int(s.num(13));
        t.bitrateKbps = int(s.num(14)); t.durationMs = s.num(15); t.lossless = s.num(16) != 0;
        t.genre = s.str(17); t.added = s.num(18);
        t.coverKey = QString::fromLatin1(t.path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        out.push_back(std::move(t));
    }
    return out;
}

QHash<QString, QPair<qint64, qint64>> Db::signatures()
{
    QHash<QString, QPair<qint64, qint64>> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT path,mtime,size FROM tracks");
    while (s.row()) out.insert(s.str(0), {s.num(1), s.num(2)});
    return out;
}

void Db::upsertTracks(const QVector<Track>& tracks)
{
    if (!m_db || tracks.isEmpty()) return;
    Stmt s(m_db, "INSERT OR REPLACE INTO tracks(path,mtime,size,title,artist,albumArtist,album,codec,trackNo,discNo,year,sampleRate,channels,bitDepth,bitrate,durationMs,lossless,genre,added) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const Track& t : tracks) {
        sqlite3_reset(s.s);
        s.text(1, t.path); s.i64(2, t.mtime); s.i64(3, t.size);
        s.text(4, t.title); s.text(5, t.artist); s.text(6, t.albumArtist); s.text(7, t.album); s.text(8, t.codec);
        s.i64(9, t.trackNo); s.i64(10, t.discNo); s.i64(11, t.year); s.i64(12, t.sampleRate); s.i64(13, t.channels);
        s.i64(14, t.bitDepth); s.i64(15, t.bitrateKbps); s.i64(16, t.durationMs); s.i64(17, t.lossless ? 1 : 0);
        s.text(18, t.genre); s.i64(19, t.added);
        s.run();
    }
}

void Db::removeTracks(const QStringList& paths)
{
    if (!m_db || paths.isEmpty()) return;
    Stmt s(m_db, "DELETE FROM tracks WHERE path=?");
    for (const QString& p : paths) { sqlite3_reset(s.s); s.text(1, p); s.run(); }
}

std::optional<Db::LyricsRec> Db::lyrics(const QString& key)
{
    if (!m_db) return std::nullopt;
    Stmt s(m_db, "SELECT provider,synced,body,fetched FROM lyrics WHERE k=?"); s.text(1, key);
    if (!s.row()) return std::nullopt;
    return LyricsRec{s.str(0), s.num(1) != 0, s.str(2), s.num(3)};
}

void Db::putLyrics(const QString& key, const LyricsRec& r)
{
    if (!m_db) return;
    Stmt s(m_db, "INSERT OR REPLACE INTO lyrics VALUES(?,?,?,?,?)");
    s.text(1, key); s.text(2, r.provider); s.i64(3, r.synced ? 1 : 0); s.text(4, r.text);
    s.i64(5, r.fetched ? r.fetched : QDateTime::currentSecsSinceEpoch()); s.run();
}

void Db::clearLyrics(const QString& key)
{
    if (!m_db) return;
    Stmt s(m_db, "DELETE FROM lyrics WHERE k=?"); s.text(1, key); s.run();
}

// ---- playlists --------------------------------------------------------------------
QVector<Db::PlaylistRow> Db::playlists()
{
    QVector<PlaylistRow> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT id,name,special,created FROM playlists ORDER BY (special='liked') DESC, id");
    while (s.row()) out.push_back({s.num(0), s.str(1), s.str(2), s.num(3)});
    return out;
}

qint64 Db::createPlaylist(const QString& name, const QString& special)
{
    if (!m_db) return 0;
    Stmt s(m_db, "INSERT INTO playlists(name,special,created) VALUES(?,?,?)");
    s.text(1, name); s.text(2, special); s.i64(3, QDateTime::currentSecsSinceEpoch()); s.run();
    return sqlite3_last_insert_rowid(m_db);
}

void Db::renamePlaylist(qint64 id, const QString& name)
{
    if (!m_db) return;
    Stmt s(m_db, "UPDATE playlists SET name=? WHERE id=?"); s.text(1, name); s.i64(2, id); s.run();
}

void Db::deletePlaylist(qint64 id)
{
    if (!m_db) return;
    { Stmt s(m_db, "DELETE FROM playlist_items WHERE playlist=?"); s.i64(1, id); s.run(); }
    { Stmt s(m_db, "DELETE FROM playlists WHERE id=?"); s.i64(1, id); s.run(); }
}

QVector<Db::PlItem> Db::playlistItems(qint64 id)
{
    QVector<PlItem> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT path,title,artist,album,durationMs FROM playlist_items WHERE playlist=? ORDER BY pos"); s.i64(1, id);
    while (s.row()) out.push_back({s.str(0), s.str(1), s.str(2), s.str(3), s.num(4)});
    return out;
}

void Db::setPlaylistItems(qint64 id, const QVector<PlItem>& items)
{
    if (!m_db) return;
    begin();
    { Stmt d(m_db, "DELETE FROM playlist_items WHERE playlist=?"); d.i64(1, id); d.run(); }
    Stmt s(m_db, "INSERT INTO playlist_items VALUES(?,?,?,?,?,?,?)");
    for (int i = 0; i < items.size(); ++i) {
        sqlite3_reset(s.s);
        s.i64(1, id); s.i64(2, i); s.text(3, items[i].path); s.text(4, items[i].title); s.text(5, items[i].artist);
        s.text(6, items[i].album); s.i64(7, items[i].durationMs); s.run();
    }
    commit();
}

// ---- listening history --------------------------------------------------------------
void Db::recordPlay(const QString& path, qint64 at, qint64 ms)
{
    if (!m_db) return;
    Stmt s(m_db, "INSERT INTO plays VALUES(?,?,?)"); s.text(1, path); s.i64(2, at); s.i64(3, ms); s.run();
}

QVector<Db::PlayRec> Db::plays(qint64 since)
{
    QVector<PlayRec> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT path,at,ms FROM plays WHERE at>=? ORDER BY at"); s.i64(1, since);
    while (s.row()) out.push_back({s.str(0), s.num(1), s.num(2)});
    return out;
}

// ---- online catalog -------------------------------------------------------------------
static Track trackFromCatRow(Stmt& s)
{
    Track t;
    const QString id = s.str(0);
    t.path = catalogPath(id);
    t.artistId = s.str(1); t.artist = s.str(2); t.title = s.str(3); t.album = s.str(4); t.albumId = s.str(5);
    t.albumArtist = s.str(6); t.coverKey = s.str(7); t.coverUrl = t.coverKey;
    t.durationMs = s.num(8); t.year = int(s.num(9)); t.trackNo = int(s.num(10)); t.discNo = int(s.num(11)); t.rank = int(s.num(12));
    t.added = s.num(14);
    t.codec = QStringLiteral("Stream");
    return t;
}

void Db::upsertCatTracks(const QVector<Track>& tracks, bool saved)
{
    if (!m_db || tracks.isEmpty()) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    begin();
    Stmt s(m_db, "INSERT INTO cat_tracks(id,artist_id,artist,title,album,album_id,album_artist,cover,duration_ms,year,track_no,disc_no,rank,saved,added)"
                 " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
                 " ON CONFLICT(id) DO UPDATE SET artist_id=excluded.artist_id, artist=excluded.artist, title=excluded.title, album=excluded.album,"
                 " album_id=excluded.album_id, album_artist=excluded.album_artist, cover=excluded.cover, duration_ms=excluded.duration_ms,"
                 " year=CASE WHEN excluded.year>0 THEN excluded.year ELSE cat_tracks.year END,"
                 " track_no=CASE WHEN excluded.track_no>0 THEN excluded.track_no ELSE cat_tracks.track_no END,"
                 " disc_no=CASE WHEN excluded.disc_no>0 THEN excluded.disc_no ELSE cat_tracks.disc_no END,"
                 " rank=excluded.rank, saved=MAX(cat_tracks.saved, excluded.saved),"
                 " added=CASE WHEN cat_tracks.saved=0 AND excluded.saved=1 THEN excluded.added ELSE cat_tracks.added END");
    for (const Track& t : tracks) {
        sqlite3_reset(s.s);
        s.text(1, catalogId(t.path)); s.text(2, t.artistId); s.text(3, t.artist); s.text(4, t.title); s.text(5, t.album);
        s.text(6, t.albumId); s.text(7, t.albumArtist); s.text(8, t.coverUrl); s.i64(9, t.durationMs); s.i64(10, t.year);
        s.i64(11, t.trackNo); s.i64(12, t.discNo); s.i64(13, t.rank); s.i64(14, saved ? 1 : 0); s.i64(15, now);
        s.run();
    }
    commit();
}

std::optional<Track> Db::catTrack(const QString& id)
{
    if (!m_db) return std::nullopt;
    Stmt s(m_db, "SELECT id,artist_id,artist,title,album,album_id,album_artist,cover,duration_ms,year,track_no,disc_no,rank,saved,added FROM cat_tracks WHERE id=?");
    s.text(1, id);
    if (!s.row()) return std::nullopt;
    return trackFromCatRow(s);
}

QVector<Track> Db::catTracks(bool savedOnly)
{
    QVector<Track> out;
    if (!m_db) return out;
    Stmt s(m_db, savedOnly ? "SELECT id,artist_id,artist,title,album,album_id,album_artist,cover,duration_ms,year,track_no,disc_no,rank,saved,added FROM cat_tracks WHERE saved=1"
                           : "SELECT id,artist_id,artist,title,album,album_id,album_artist,cover,duration_ms,year,track_no,disc_no,rank,saved,added FROM cat_tracks");
    while (s.row()) out.push_back(trackFromCatRow(s));
    return out;
}

void Db::unsaveCatTracks(const QStringList& ids)
{
    if (!m_db || ids.isEmpty()) return;
    begin();
    Stmt s(m_db, "UPDATE cat_tracks SET saved=0 WHERE id=?");
    for (const QString& id : ids) { sqlite3_reset(s.s); s.text(1, id); s.run(); }
    commit();
}

void Db::upsertCatArtist(const CatArtist& a, bool saved)
{
    if (!m_db || a.id.isEmpty()) return;
    Stmt s(m_db, "INSERT INTO cat_artists(id,name,key,picture,fans,saved,added) VALUES(?,?,?,?,?,?,?)"
                 " ON CONFLICT(id) DO UPDATE SET name=excluded.name, key=excluded.key,"
                 " picture=CASE WHEN excluded.picture<>'' THEN excluded.picture ELSE cat_artists.picture END,"
                 " fans=CASE WHEN excluded.fans>0 THEN excluded.fans ELSE cat_artists.fans END, saved=MAX(cat_artists.saved, excluded.saved)");
    s.text(1, a.id); s.text(2, a.name); s.text(3, foldText(a.name)); s.text(4, a.picture); s.i64(5, a.fans);
    s.i64(6, saved ? 1 : 0); s.i64(7, QDateTime::currentSecsSinceEpoch()); s.run();
}

static Db::CatArtist artistFromRow(Stmt& s) { return {s.str(0), s.str(1), s.str(2), s.num(3), s.num(4) != 0}; }

std::optional<Db::CatArtist> Db::catArtist(const QString& id)
{
    if (!m_db) return std::nullopt;
    Stmt s(m_db, "SELECT id,name,picture,fans,saved FROM cat_artists WHERE id=?"); s.text(1, id);
    if (!s.row()) return std::nullopt;
    return artistFromRow(s);
}

std::optional<Db::CatArtist> Db::catArtistByName(const QString& name)
{
    if (!m_db) return std::nullopt;
    Stmt s(m_db, "SELECT id,name,picture,fans,saved FROM cat_artists WHERE key=? ORDER BY fans DESC LIMIT 1"); s.text(1, foldText(name));
    if (!s.row()) return std::nullopt;
    return artistFromRow(s);
}

QVector<Db::CatArtist> Db::savedCatArtists()
{
    QVector<CatArtist> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT id,name,picture,fans,saved FROM cat_artists WHERE saved=1 ORDER BY name");
    while (s.row()) out.push_back(artistFromRow(s));
    return out;
}

void Db::unsaveCatArtist(const QString& id)
{
    if (!m_db) return;
    Stmt s(m_db, "UPDATE cat_artists SET saved=0 WHERE id=?"); s.text(1, id); s.run();
}

// ---- HTTP cache ------------------------------------------------------------------------
std::optional<QByteArray> Db::cacheGet(const QString& url, qint64 maxAgeSec)
{
    if (!m_db) return std::nullopt;
    Stmt s(m_db, "SELECT body,fetched FROM http_cache WHERE url=?"); s.text(1, url);
    if (!s.row()) return std::nullopt;
    if (QDateTime::currentSecsSinceEpoch() - s.num(1) > maxAgeSec) return std::nullopt;
    const int n = sqlite3_column_bytes(s.s, 0);
    return QByteArray(static_cast<const char*>(sqlite3_column_blob(s.s, 0)), n);
}

void Db::cachePut(const QString& url, const QByteArray& body)
{
    if (!m_db) return;
    Stmt s(m_db, "INSERT OR REPLACE INTO http_cache(url,body,fetched) VALUES(?,?,?)");
    s.text(1, url);
    sqlite3_bind_blob(s.s, 2, body.constData(), body.size(), SQLITE_TRANSIENT);
    s.i64(3, QDateTime::currentSecsSinceEpoch()); s.run();
}

void Db::cachePrune(qint64 olderThanSec)
{
    if (!m_db) return;
    Stmt s(m_db, "DELETE FROM http_cache WHERE fetched < ?"); s.i64(1, QDateTime::currentSecsSinceEpoch() - olderThanSec); s.run();
}

// ---- skips --------------------------------------------------------------------------------
void Db::recordSkip(const QString& path, qint64 at)
{
    if (!m_db) return;
    Stmt s(m_db, "INSERT INTO skips VALUES(?,?)"); s.text(1, path); s.i64(2, at); s.run();
}

QVector<Db::PlayRec> Db::skips(qint64 since)
{
    QVector<PlayRec> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT path,at FROM skips WHERE at>=? ORDER BY at"); s.i64(1, since);
    while (s.row()) out.push_back({s.str(0), s.num(1), 0});
    return out;
}


// ---- the song index ---------------------------------------------------------------------------------

namespace {
// Deezer cover addresses are "<prefix><32 hex chars>/<size>-...jpg": keep only the hex part (about 70% smaller).
const QString kCoverPrefix = QStringLiteral("https://cdn-images.dzcdn.net/images/cover/");
const QString kCoverSuffix = QStringLiteral("/500x500-000000-80-0-0.jpg");
QString packCover(const QString& url)
{
    if (url.startsWith(kCoverPrefix) && url.endsWith(kCoverSuffix)) return url.mid(kCoverPrefix.size(), url.size() - kCoverPrefix.size() - kCoverSuffix.size());
    return url;
}
QString unpackCover(const QString& s)
{
    return (s.size() == 32 && !s.contains(QLatin1Char(':'))) ? kCoverPrefix + s + kCoverSuffix : s;
}
Db::IdxSong readSong(const Stmt& s)
{
    Db::IdxSong x;
    x.h = s.num(0); x.path = s.str(1); x.title = s.str(2); x.artist = s.str(3); x.key = s.str(4); x.album = s.str(5);
    x.artistId = s.str(6); x.cover = unpackCover(s.str(7)); x.durationMs = s.num(8); x.rank = int(s.num(9));
    x.seen = int(s.num(10)); x.flags = int(s.num(11));
    return x;
}
const char* kSongCols = "h,p,t,a,k,al,aid,c,d,r,n,f";
} // namespace

void Db::idxUpsertSongs(const QVector<IdxSong>& songs)
{
    if (!m_db || songs.isEmpty()) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    begin();
    for (const IdxSong& x : songs) {
        // a catalog address beats an extension one (it resolves faster and is stable); never lose a cover or album once known
        Stmt s(m_db, "INSERT INTO idx_songs(h,p,t,a,k,al,aid,c,d,r,n,f,l) VALUES(?,?,?,?,?,?,?,?,?,?,1,?,?)"
                     " ON CONFLICT(h) DO UPDATE SET n=n+1, f=f|excluded.f, l=excluded.l, r=MAX(r,excluded.r),"
                     " p=CASE WHEN p LIKE 'lumen://dz/%' THEN p WHEN excluded.p LIKE 'lumen://dz/%' THEN excluded.p ELSE p END,"
                     " al=CASE WHEN al='' THEN excluded.al ELSE al END, aid=CASE WHEN aid='' THEN excluded.aid ELSE aid END,"
                     " c=CASE WHEN c='' THEN excluded.c ELSE c END, d=CASE WHEN d=0 THEN excluded.d ELSE d END");
        s.i64(1, x.h); s.text(2, x.path); s.text(3, x.title); s.text(4, x.artist); s.text(5, x.key); s.text(6, x.album);
        s.text(7, x.artistId); s.text(8, packCover(x.cover)); s.i64(9, x.durationMs); s.i64(10, x.rank); s.i64(11, x.flags); s.i64(12, now);
        s.run();
    }
    commit();
}

QVector<Db::IdxSong> Db::idxSongsByArtist(const QString& key, int limit)
{
    QVector<IdxSong> out;
    if (!m_db || key.isEmpty()) return out;
    // what you listened to first, then the artist's best-ranked songs
    const QByteArray sql = QByteArray("SELECT ") + kSongCols + " FROM idx_songs WHERE k=? ORDER BY (f&1) DESC, r DESC, n DESC LIMIT ?";
    Stmt s(m_db, sql.constData());
    s.text(1, key); s.i64(2, limit);
    while (s.row()) out.push_back(readSong(s));
    return out;
}

QVector<Db::IdxSong> Db::idxSongsByHash(const QVector<qint64>& hashes)
{
    QVector<IdxSong> out;
    if (!m_db) return out;
    const QByteArray sql = QByteArray("SELECT ") + kSongCols + " FROM idx_songs WHERE h=?";
    for (qint64 h : hashes) {
        Stmt s(m_db, sql.constData());
        s.i64(1, h);
        if (s.row()) out.push_back(readSong(s));
    }
    return out;
}

void Db::idxTouchArtist(const QString& key, const QString& name, const QString& cid, double addWeight)
{
    if (!m_db || key.isEmpty()) return;
    Stmt s(m_db, "INSERT INTO idx_artists(k,name,cid,w,cr) VALUES(?,?,?,?,0) ON CONFLICT(k) DO UPDATE SET w=w+excluded.w,"
                 " cid=CASE WHEN cid='' THEN excluded.cid ELSE cid END, name=CASE WHEN name='' THEN excluded.name ELSE name END");
    s.text(1, key); s.text(2, name); s.text(3, cid); s.dbl(4, addWeight); s.run();
}

std::optional<Db::IdxArtist> Db::idxArtist(const QString& key)
{
    if (!m_db) return std::nullopt;
    Stmt s(m_db, "SELECT k,name,cid,w,cr FROM idx_artists WHERE k=?");
    s.text(1, key);
    if (!s.row()) return std::nullopt;
    return IdxArtist{s.str(0), s.str(1), s.str(2), s.real(3), s.num(4)};
}

QVector<Db::IdxArtist> Db::idxArtistsToCrawl(int n, qint64 notCrawledSince)
{
    QVector<IdxArtist> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT k,name,cid,w,cr FROM idx_artists WHERE cr < ? ORDER BY w DESC LIMIT ?");
    s.i64(1, notCrawledSince); s.i64(2, n);
    while (s.row()) out.push_back({s.str(0), s.str(1), s.str(2), s.real(3), s.num(4)});
    return out;
}

QVector<Db::IdxArtist> Db::idxTopArtists(int n)
{
    QVector<IdxArtist> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT k,name,cid,w,cr FROM idx_artists WHERE w >= 1 ORDER BY w DESC LIMIT ?");
    s.i64(1, n);
    while (s.row()) out.push_back({s.str(0), s.str(1), s.str(2), s.real(3), s.num(4)});
    return out;
}

void Db::idxMarkCrawled(const QString& key, qint64 at)
{
    if (!m_db) return;
    Stmt s(m_db, "UPDATE idx_artists SET cr=? WHERE k=?");
    s.i64(1, at); s.text(2, key); s.run();
}

void Db::idxAddEdge(const QString& a, const QString& b, double w)
{
    if (!m_db || a.isEmpty() || b.isEmpty() || a == b) return;
    for (int dir = 0; dir < 2; ++dir) {
        Stmt s(m_db, "INSERT INTO idx_edges(a,b,w) VALUES(?,?,?) ON CONFLICT(a,b) DO UPDATE SET w=MIN(8.0, w+excluded.w)");
        s.text(1, dir ? b : a); s.text(2, dir ? a : b); s.dbl(3, w); s.run();
    }
}

QVector<QPair<QString, double>> Db::idxRelated(const QString& key, int n)
{
    QVector<QPair<QString, double>> out;
    if (!m_db || key.isEmpty()) return out;
    Stmt s(m_db, "SELECT b,w FROM idx_edges WHERE a=? ORDER BY w DESC LIMIT ?");
    s.text(1, key); s.i64(2, n);
    while (s.row()) out.push_back({s.str(0), s.real(1)});
    return out;
}

void Db::idxAddPair(qint64 from, qint64 to, double w)
{
    if (!m_db || from == to) return;
    Stmt s(m_db, "INSERT INTO idx_pairs(a,b,w) VALUES(?,?,?) ON CONFLICT(a,b) DO UPDATE SET w=MIN(8.0, w+excluded.w)");
    s.i64(1, from); s.i64(2, to); s.dbl(3, w); s.run();
}

QVector<QPair<qint64, double>> Db::idxPairsFrom(qint64 h, int n)
{
    QVector<QPair<qint64, double>> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT b,w FROM idx_pairs WHERE a=? ORDER BY w DESC LIMIT ?");
    s.i64(1, h); s.i64(2, n);
    while (s.row()) out.push_back({s.num(0), s.real(1)});
    return out;
}

void Db::idxNoteSearch(const QString& query, const QString& artistKey, qint64 at)
{
    if (!m_db || query.isEmpty()) return;
    Stmt s(m_db, "INSERT INTO idx_searches(q,n,at,k) VALUES(?,1,?,?) ON CONFLICT(q) DO UPDATE SET n=n+1, at=excluded.at,"
                 " k=CASE WHEN excluded.k<>'' THEN excluded.k ELSE k END");
    s.text(1, query); s.i64(2, at); s.text(3, artistKey); s.run();
}

QVector<QPair<QString, QString>> Db::idxRecentSearches(int n)
{
    QVector<QPair<QString, QString>> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT q,k FROM idx_searches ORDER BY at DESC LIMIT ?");
    s.i64(1, n);
    while (s.row()) out.push_back({s.str(0), s.str(1)});
    return out;
}

Db::IdxCounts Db::idxCounts()
{
    IdxCounts c;
    if (!m_db) return c;
    auto one = [&](const char* sql) { Stmt s(m_db, sql); return s.row() ? s.num(0) : 0; };
    c.songs = one("SELECT COUNT(*) FROM idx_songs");
    c.artists = one("SELECT COUNT(*) FROM idx_artists");
    c.edges = one("SELECT COUNT(*) FROM idx_edges") / 2 + one("SELECT COUNT(*) FROM idx_pairs");
    c.searches = one("SELECT COUNT(*) FROM idx_searches");
    c.crawled = one("SELECT COUNT(*) FROM idx_artists WHERE cr > 0");
    return c;
}

void Db::idxPrune(qint64 maxSongs)
{
    if (!m_db) return;
    Stmt cnt(m_db, "SELECT COUNT(*) FROM idx_songs");
    const qint64 have = cnt.row() ? cnt.num(0) : 0;
    if (have <= maxSongs) return;
    // forget the least valuable first: never listened to, seen once, longest ago
    Stmt s(m_db, "DELETE FROM idx_songs WHERE h IN (SELECT h FROM idx_songs WHERE (f&1)=0 ORDER BY n ASC, l ASC LIMIT ?)");
    s.i64(1, have - maxSongs + maxSongs / 10); s.run();
}
