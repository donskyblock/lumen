#pragma once
#include <QVector>
#include <QString>

namespace SpotifyImport {
struct Track {
    QString title, artist, album, uri;
    qint64 durationMs = 0;
};
struct Playlist {
    QString name;
    bool likedSongs = false;
    QVector<Track> tracks;
};
struct Result {
    QVector<Playlist> playlists;
    QString error;
};

// Reads Spotify privacy-export ZIP/JSON and Exportify CSV/JSON. Parsing is local;
// unrelated account files in a ZIP are ignored.
Result readFile(const QString& path);
}
