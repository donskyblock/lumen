#pragma once
#include <QString>
#include <QVector>
#include <QMetaType>

struct Track {
    QString path;
    QString title;
    QString artist;
    QString albumArtist;
    QString album;
    QString codec;        // display name: FLAC, MP3, WAV ...
    QString coverKey;     // base64url(path), see Covers::key()
    int trackNo = 0;
    int discNo = 0;
    int year = 0;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    int bitrateKbps = 0;
    qint64 durationMs = 0;
    qint64 mtime = 0;     // for incremental rescans
    qint64 size = 0;
    QString coverUrl;     // remote tracks only
    QString genre;
    QString folded;       // accent-folded title/artist/album/genre for search (not stored)
    qint64 added = 0;     // when Lumen first saw the file
    QString headers;      // remote tracks only: "Key: Value\r\n..." for FFmpeg
    bool lossless = false;
    bool explicitLyrics = false; // online catalog metadata
    // Catalog (online) tracks only. path is "lumen://dz/<id>"; the playable address is found at play time.
    QString artistId, albumId;
    int rank = 0;                 // catalog popularity, higher = more popular

    // "FLAC · 24-bit / 96 kHz"  or  "MP3 · 320 kbps"
    QString quality() const;
};

// Online catalog tracks live under a synthetic path so plays, likes, playlists
// and the saved queue all work on them exactly like on files.
inline bool isCatalogPath(const QString& p) { return p.startsWith(QLatin1String("lumen://dz/")); }
inline QString catalogPath(const QString& id) { return QStringLiteral("lumen://dz/") + id; }
inline QString catalogId(const QString& path) { return path.mid(11); }
// Songs offered by an extension (autoplay): "lumen://ext/<plugin id>/<item id>", resolved when played.
inline bool isExtPath(const QString& p) { return p.startsWith(QLatin1String("lumen://ext/")); }
inline QString extPath(const QString& plugin, const QString& id) { return QStringLiteral("lumen://ext/") + plugin + QLatin1Char('/') + id; }
inline bool needsResolve(const QString& p) { return isCatalogPath(p) || isExtPath(p); }
// A remote address that is only good for one session (extension streams).
inline bool isTransientRemote(const QString& p) { return p.contains(QLatin1String("://")) && !isCatalogPath(p); }
Q_DECLARE_METATYPE(Track)
Q_DECLARE_METATYPE(QVector<Track>)
