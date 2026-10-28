#include "Probe.h"
#include "Covers.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
}

QString Track::quality() const
{
    QString q = codec;
    if (lossless) {
        QStringList parts;
        if (bitDepth > 0) parts << QString::number(bitDepth) + "-bit";
        if (sampleRate > 0) {
            const double khz = sampleRate / 1000.0;
            parts << QString::number(khz, 'f', (sampleRate % 1000) ? 1 : 0) + " kHz";
        }
        if (!parts.isEmpty()) q += QStringLiteral(" \u00b7 ") + parts.join(" / ");
    } else if (bitrateKbps > 0) {
        q += QStringLiteral(" \u00b7 ") + QString::number(bitrateKbps) + " kbps";
    }
    return q;
}

bool isAudioFile(const QString& s)
{
    static const QSet<QString> ext = {
        "flac", "wav", "mp3", "ogg", "oga", "opus", "m4a", "aac", "alac",
        "aiff", "aif", "ape", "wv", "wma", "mka", "dsf"};
    return ext.contains(s);
}

static QString tag(AVDictionary* a, AVDictionary* b, const char* key)
{
    for (AVDictionary* d : {a, b}) {
        if (!d) continue;
        if (AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0))
            return QString::fromUtf8(e->value).trimmed();
    }
    return {};
}

static QString codecDisplay(const AVCodecDescriptor* d, const QString& suffix)
{
    if (!d) return suffix.toUpper();
    const QString n = QString::fromLatin1(d->name);
    if (n.startsWith("pcm_")) return suffix == "aif" || suffix == "aiff" ? "AIFF" : "WAV";
    if (n == "vorbis") return "Vorbis";
    if (n == "wavpack") return "WavPack";
    if (n == "ape") return "APE";
    return n.toUpper();
}

std::optional<Track> probeFile(const QString& path)
{
    AVFormatContext* fmt = nullptr;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "probesize", "131072", 0);
    av_dict_set(&opts, "analyzeduration", "200000", 0);
    const QByteArray p = path.toUtf8();
    int rc = avformat_open_input(&fmt, p.constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (rc < 0) return std::nullopt;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return std::nullopt; }

    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (idx < 0) { avformat_close_input(&fmt); return std::nullopt; }
    AVStream* st = fmt->streams[idx];
    AVCodecParameters* par = st->codecpar;

    Track t;
    t.path = path;
    t.coverKey = Covers::key(path);
    const QFileInfo fi(path);
    t.mtime = fi.lastModified().toSecsSinceEpoch();
    t.size = fi.size();
    const QString suffix = fi.suffix().toLower();

    t.title       = tag(fmt->metadata, st->metadata, "title");
    t.artist      = tag(fmt->metadata, st->metadata, "artist");
    t.albumArtist = tag(fmt->metadata, st->metadata, "album_artist");
    if (t.albumArtist.isEmpty()) t.albumArtist = tag(fmt->metadata, st->metadata, "albumartist");
    t.album       = tag(fmt->metadata, st->metadata, "album");
    t.trackNo     = tag(fmt->metadata, st->metadata, "track").section('/', 0, 0).toInt();
    t.discNo      = tag(fmt->metadata, st->metadata, "disc").section('/', 0, 0).toInt();
    t.genre       = tag(fmt->metadata, st->metadata, "genre");
    QString date  = tag(fmt->metadata, st->metadata, "date");
    if (date.isEmpty()) date = tag(fmt->metadata, st->metadata, "year");
    t.year        = date.left(4).toInt();

    if (t.title.isEmpty())  t.title = fi.completeBaseName();
    if (t.artist.isEmpty()) t.artist = t.albumArtist.isEmpty() ? QStringLiteral("Unknown artist") : t.albumArtist;
    if (t.album.isEmpty())  t.album = fi.dir().dirName();
    if (t.albumArtist.isEmpty()) t.albumArtist = t.artist;

    t.sampleRate = par->sample_rate;
    t.channels   = par->ch_layout.nb_channels;
    t.bitDepth   = par->bits_per_raw_sample ? par->bits_per_raw_sample : par->bits_per_coded_sample;
    const int64_t br = par->bit_rate ? par->bit_rate : fmt->bit_rate;
    t.bitrateKbps = int((br + 500) / 1000);
    if (fmt->duration > 0) t.durationMs = fmt->duration / 1000;
    else if (st->duration > 0) t.durationMs = int64_t(st->duration * av_q2d(st->time_base) * 1000.0);

    const AVCodecDescriptor* d = avcodec_descriptor_get(par->codec_id);
    t.lossless = d && (d->props & AV_CODEC_PROP_LOSSLESS) && !(d->props & AV_CODEC_PROP_LOSSY);
    t.codec = codecDisplay(d, suffix);

    avformat_close_input(&fmt);
    return t;
}
