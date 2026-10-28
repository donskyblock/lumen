#include "SpotifyImport.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QObject>
#include <QUrl>
#include <zlib.h>
#include <algorithm>
#include <cstring>

namespace SpotifyImport {
namespace {
constexpr qint64 kMaxArchiveBytes = 256LL * 1024 * 1024;
constexpr quint32 kMaxEntryBytes = 32U * 1024 * 1024;
constexpr quint64 kMaxExpandedBytes = 128ULL * 1024 * 1024;

quint16 u16(const char* p)
{
    return quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8);
}
quint32 u32(const char* p)
{
    return quint32(quint8(p[0])) | (quint32(quint8(p[1])) << 8)
         | (quint32(quint8(p[2])) << 16) | (quint32(quint8(p[3])) << 24);
}
QString normalizedKey(QString s)
{
    s = s.toLower();
    s.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return s;
}
QString basename(QString name)
{
    name.replace('\\', '/');
    return name.section('/', -1);
}
bool isLikedSourceName(const QString& name)
{
    const QString lower = name.toLower();
    return lower.contains("liked") || lower.contains("favorite") || lower.contains("favourite")
        || lower.contains("yourlibrary") || lower.contains("savedtracks");
}

bool unzipSelected(const QByteArray& zip, QHash<QString, QByteArray>* files, QString* error)
{
    const QByteArray eocd("PK\x05\x06", 4);
    if (zip.size() < 22) { *error = QObject::tr("That ZIP archive is too small to be valid."); return false; }
    const qsizetype searchFrom = std::max<qsizetype>(0, zip.size() - 65557);
    const qsizetype end = zip.lastIndexOf(eocd, zip.size() - 22);
    if (end < searchFrom || end + 22 > zip.size()) { *error = QObject::tr("That ZIP archive has no valid directory."); return false; }
    const quint16 count = u16(zip.constData() + end + 10);
    const quint32 directorySize = u32(zip.constData() + end + 12);
    const quint32 directoryOffset = u32(zip.constData() + end + 16);
    if (count == 0xffff || directorySize == 0xffffffffU || directoryOffset == 0xffffffffU
        || quint64(directoryOffset) + directorySize > quint64(zip.size()) || count > 10000) {
        *error = QObject::tr("This ZIP layout isn't supported."); return false;
    }

    quint64 expandedTotal = 0;
    quint64 pos = directoryOffset;
    for (quint16 i = 0; i < count; ++i) {
        if (pos + 46 > quint64(zip.size()) || u32(zip.constData() + pos) != 0x02014b50U) {
            *error = QObject::tr("The ZIP directory is damaged."); return false;
        }
        const char* c = zip.constData() + pos;
        const quint16 flags = u16(c + 8), method = u16(c + 10);
        const quint32 crc = u32(c + 16), compressed = u32(c + 20), uncompressed = u32(c + 24);
        const quint16 nameLen = u16(c + 28), extraLen = u16(c + 30), commentLen = u16(c + 32);
        const quint32 localOffset = u32(c + 42);
        const quint64 recordEnd = pos + 46ULL + nameLen + extraLen + commentLen;
        if (recordEnd > quint64(zip.size())) { *error = QObject::tr("The ZIP directory is damaged."); return false; }
        const QString fullName = QString::fromUtf8(c + 46, nameLen);
        const QString fileName = basename(fullName);
        const QString lower = fileName.toLower();
        const bool selected = !fullName.endsWith('/') && ((lower.startsWith("playlist") && lower.endsWith(".json"))
                           || lower == "yourlibrary.json" || lower.endsWith(".csv"));
        pos = recordEnd;
        if (!selected || fileName.endsWith('/')) continue;
        if ((flags & 1) || uncompressed > kMaxEntryBytes || compressed > kMaxEntryBytes
            || expandedTotal + uncompressed > kMaxExpandedBytes) {
            *error = QObject::tr("The export is encrypted or larger than Lumen's import limit."); return false;
        }
        if (method != 0 && method != 8) { *error = QObject::tr("The export uses an unsupported ZIP compression method."); return false; }
        if (quint64(localOffset) + 30 > quint64(zip.size()) || u32(zip.constData() + localOffset) != 0x04034b50U) {
            *error = QObject::tr("The ZIP entry is damaged."); return false;
        }
        const char* local = zip.constData() + localOffset;
        const quint16 localNameLen = u16(local + 26), localExtraLen = u16(local + 28);
        const quint64 dataOffset = quint64(localOffset) + 30 + localNameLen + localExtraLen;
        if (dataOffset + compressed > quint64(zip.size())) { *error = QObject::tr("The ZIP entry is truncated."); return false; }
        QByteArray data;
        data.resize(int(uncompressed));
        if (method == 0) {
            if (compressed != uncompressed) { *error = QObject::tr("The ZIP entry has inconsistent sizes."); return false; }
            if (uncompressed) std::memcpy(data.data(), zip.constData() + dataOffset, uncompressed);
        } else {
            z_stream stream{};
            char emptyOut = 0;
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(zip.constData() + dataOffset));
            stream.avail_in = compressed;
            stream.next_out = reinterpret_cast<Bytef*>(uncompressed ? data.data() : &emptyOut);
            stream.avail_out = std::max<quint32>(uncompressed, 1);
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) { *error = QObject::tr("Couldn't initialize ZIP decompression."); return false; }
            const int status = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (status != Z_STREAM_END || stream.total_out != uncompressed) { *error = QObject::tr("A ZIP entry couldn't be decompressed."); return false; }
        }
        if (crc32(0L, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size())) != crc) {
            *error = QObject::tr("A ZIP entry failed its integrity check."); return false;
        }
        expandedTotal += uncompressed;
        files->insert(fileName, std::move(data));
    }
    return true;
}

QString fieldName(const QJsonObject& o, const QStringList& names)
{
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString key = normalizedKey(it.key());
        for (const QString& name : names) if (key == normalizedKey(name)) {
            if (it.value().isString()) return it.value().toString();
            if (it.value().isDouble()) return QString::number(it.value().toDouble());
            if (it.value().isArray()) {
                QStringList values;
                for (const QJsonValue& v : it.value().toArray()) {
                    if (v.isString()) values << v.toString();
                    else if (v.isObject()) values << fieldName(v.toObject(), {"name", "artistName"});
                }
                values.removeAll(QString());
                return values.join(QStringLiteral("; "));
            }
        }
    }
    return {};
}

Track trackFromObject(const QJsonObject& row)
{
    Track track;
    track.title = fieldName(row, {"trackName", "track name", "name", "title", "track"});
    track.artist = fieldName(row, {"artistName", "artist name(s)", "artist name", "artists", "artist"});
    track.album = fieldName(row, {"albumName", "album name", "album"});
    track.uri = fieldName(row, {"trackUri", "track uri", "spotify uri", "uri", "track id"});
    const QString duration = fieldName(row, {"durationMs", "duration_ms", "duration ms"});
    track.durationMs = duration.toLongLong();
    if (track.durationMs > 0 && track.durationMs < 10000) track.durationMs *= 1000;
    return track;
}

QVector<Track> tracksFromArray(const QJsonArray& array)
{
    QVector<Track> tracks;
    for (const QJsonValue& value : array) {
        QJsonObject row = value.toObject();
        for (int depth = 0; depth < 4; ++depth) {
            if (row.value("track").isObject()) row = row.value("track").toObject();
            else if (row.value("item").isObject()) row = row.value("item").toObject();
            else break;
        }
        Track track = trackFromObject(row);
        if (!track.title.isEmpty()) tracks.push_back(std::move(track));
    }
    return tracks;
}

QVector<Playlist> playlistsFromJson(const QByteArray& bytes, const QString& sourceName, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) { *error = parseError.errorString(); return {}; }
    const QString lowerName = sourceName.toLower();
    QVector<Playlist> out;
    const QJsonObject root = doc.object();
    const QJsonArray playlistArray = root.value("playlists").toArray();
    if (!playlistArray.isEmpty()) {
        for (const QJsonValue& v : playlistArray) {
            const QJsonObject playlistObject = v.toObject();
            Playlist playlist;
            playlist.name = fieldName(playlistObject, {"name", "playlistName", "title"});
            playlist.likedSongs = isLikedSourceName(playlist.name);
            playlist.tracks = tracksFromArray(playlistObject.value("items").toArray());
            if (!playlist.tracks.isEmpty()) out.push_back(std::move(playlist));
        }
    } else {
        QJsonArray rows = doc.isArray() ? doc.array() : QJsonArray();
        if (root.value("tracks").isArray()) rows = root.value("tracks").toArray();
        else if (root.value("items").isArray()) rows = root.value("items").toArray();
        Playlist playlist;
        playlist.likedSongs = isLikedSourceName(lowerName);
        playlist.name = fieldName(root, {"name", "playlistName", "title"});
        if (playlist.name.isEmpty()) playlist.name = playlist.likedSongs ? QStringLiteral("Liked Songs") : QFileInfo(sourceName).completeBaseName();
        playlist.tracks = tracksFromArray(rows);
        if (!playlist.tracks.isEmpty()) out.push_back(std::move(playlist));
    }
    return out;
}

QVector<QStringList> parseCsv(const QByteArray& bytes)
{
    QVector<QStringList> rows;
    QString cell;
    QStringList row;
    const QString text = QString::fromUtf8(bytes);
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == QLatin1Char('"') && i + 1 < text.size() && text[i + 1] == QLatin1Char('"')) { cell += c; ++i; }
            else if (c == QLatin1Char('"')) quoted = false;
            else cell += c;
        } else if (c == QLatin1Char('"')) quoted = true;
        else if (c == QLatin1Char(',')) { row << cell; cell.clear(); }
        else if (c == QLatin1Char('\n')) { row << cell; cell.clear(); if (!row.isEmpty()) rows << row; row.clear(); }
        else if (c != QLatin1Char('\r')) cell += c;
    }
    if (!cell.isEmpty() || !row.isEmpty()) { row << cell; rows << row; }
    return rows;
}

QVector<Playlist> playlistsFromCsv(const QByteArray& bytes, const QString& sourceName, QString* error)
{
    const QVector<QStringList> rows = parseCsv(bytes);
    if (rows.size() < 2) { *error = QObject::tr("The CSV has no track rows."); return {}; }
    QHash<QString, int> columns;
    for (int i = 0; i < rows.first().size(); ++i) columns.insert(normalizedKey(rows.first()[i]), i);
    auto col = [&](const QStringList& aliases) {
        for (const QString& alias : aliases) if (columns.contains(normalizedKey(alias))) return columns.value(normalizedKey(alias));
        return -1;
    };
    const int titleCol = col({"Track Name", "Track Title", "Track", "Song", "Name", "Title"});
    const int artistCol = col({"Artist Name(s)", "Artist Name", "Artist(s)", "Artist", "Artists"});
    const int albumCol = col({"Album Name", "Album Title", "Album"});
    const int uriCol = col({"Track URI", "Spotify URI", "Spotify URL", "Track URL", "URI", "Track ID"});
    const int durationCol = col({"Duration_ms", "Duration ms", "Duration"});
    if (titleCol < 0 || artistCol < 0) { *error = QObject::tr("CSV needs track title and artist columns."); return {}; }
    Playlist playlist;
    const QString stem = QFileInfo(sourceName).completeBaseName();
    playlist.likedSongs = isLikedSourceName(stem);
    playlist.name = playlist.likedSongs ? QStringLiteral("Liked Songs") : (stem.isEmpty() ? QStringLiteral("Spotify Import") : stem);
    for (int r = 1; r < rows.size(); ++r) {
        const QStringList& row = rows[r];
        auto value = [&](int c) { return c >= 0 && c < row.size() ? row[c].trimmed() : QString(); };
        Track track{value(titleCol), value(artistCol), value(albumCol), value(uriCol), value(durationCol).toLongLong()};
        if (!track.title.isEmpty()) playlist.tracks.push_back(std::move(track));
    }
    if (playlist.tracks.isEmpty()) *error = QObject::tr("The CSV contains no recognizable songs.");
    else return {playlist};
    return {};
}

} // namespace

Result readFile(const QString& path)
{
    Result result;
    QFile file(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = QObject::tr("Couldn't open that file."); return result; }
    if (file.size() > kMaxArchiveBytes) { result.error = QObject::tr("Choose the Spotify Account data export only; this archive is over 256 MB."); return result; }
    QByteArray bytes = file.readAll();
    const QString name = QFileInfo(file.fileName()).fileName();
    if (name.endsWith(".zip", Qt::CaseInsensitive)) {
        QHash<QString, QByteArray> entries;
        if (!unzipSelected(bytes, &entries, &result.error)) return result;
        QString firstError;
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            QString error;
            QVector<Playlist> found = it.key().endsWith(".csv", Qt::CaseInsensitive)
                ? playlistsFromCsv(it.value(), it.key(), &error)
                : playlistsFromJson(it.value(), it.key(), &error);
            if (!error.isEmpty() && firstError.isEmpty()) firstError = error;
            result.playlists += found;
        }
        if (result.playlists.isEmpty()) result.error = firstError;
    } else if (name.endsWith(".csv", Qt::CaseInsensitive)) {
        result.playlists = playlistsFromCsv(bytes, name, &result.error);
    } else {
        result.playlists = playlistsFromJson(bytes, name, &result.error);
    }
    if (result.playlists.isEmpty() && result.error.isEmpty())
        result.error = QObject::tr("No playlists or liked songs were found in that export.");
    return result;
}
} // namespace SpotifyImport
