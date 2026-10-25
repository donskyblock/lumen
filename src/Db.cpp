#include "Db.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <sqlite3.h>

namespace {
struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
    ~Stmt() { sqlite3_finalize(s); }
    void text(int i, const QString& v) { const QByteArray b = v.toUtf8(); sqlite3_bind_text(s, i, b.constData(), b.size(), SQLITE_TRANSIENT); }
    void i64(int i, qint64 v) { sqlite3_bind_int64(s, i, v); }
    bool row() { return sqlite3_step(s) == SQLITE_ROW; }
    void run() { sqlite3_step(s); }
    QString str(int c) const { return QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s, c))); }
    qint64 num(int c) const { return sqlite3_column_int64(s, c); }
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
         "CREATE TABLE IF NOT EXISTS lyrics(k TEXT PRIMARY KEY, provider TEXT, synced INTEGER, body TEXT, fetched INTEGER);");
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
    Stmt s(m_db, "SELECT path,mtime,size,title,artist,albumArtist,album,codec,trackNo,discNo,year,sampleRate,channels,bitDepth,bitrate,durationMs,lossless FROM tracks");
    while (s.row()) {
        Track t;
        t.path = s.str(0); t.mtime = s.num(1); t.size = s.num(2);
        t.title = s.str(3); t.artist = s.str(4); t.albumArtist = s.str(5); t.album = s.str(6); t.codec = s.str(7);
        t.trackNo = int(s.num(8)); t.discNo = int(s.num(9)); t.year = int(s.num(10));
        t.sampleRate = int(s.num(11)); t.channels = int(s.num(12)); t.bitDepth = int(s.num(13));
        t.bitrateKbps = int(s.num(14)); t.durationMs = s.num(15); t.lossless = s.num(16) != 0;
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
    Stmt s(m_db, "INSERT OR REPLACE INTO tracks VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const Track& t : tracks) {
        sqlite3_reset(s.s);
        s.text(1, t.path); s.i64(2, t.mtime); s.i64(3, t.size);
        s.text(4, t.title); s.text(5, t.artist); s.text(6, t.albumArtist); s.text(7, t.album); s.text(8, t.codec);
        s.i64(9, t.trackNo); s.i64(10, t.discNo); s.i64(11, t.year); s.i64(12, t.sampleRate); s.i64(13, t.channels);
        s.i64(14, t.bitDepth); s.i64(15, t.bitrateKbps); s.i64(16, t.durationMs); s.i64(17, t.lossless ? 1 : 0);
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
