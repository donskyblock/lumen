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

    void begin();
    void commit();

private:
    Db();
    ~Db();
    void exec(const char* sql);
    sqlite3* m_db = nullptr;
    QString m_dir;
};
