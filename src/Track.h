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
    bool lossless = false;

    // "FLAC · 24-bit / 96 kHz"  or  "MP3 · 320 kbps"
    QString quality() const;
};
Q_DECLARE_METATYPE(Track)
Q_DECLARE_METATYPE(QVector<Track>)
