#include "Metadata.h"
#include "Db.h"
#include "TextUtil.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kTtl = 13 * 86400;             // Catalog::Catalog prunes the HTTP cache at 14 days
constexpr double kAccept = 0.70;

// How likely is this candidate to be the very recording we asked about? 0 = no.
double matchScore(const QString& wantTitle, const QString& wantArtist, qint64 wantDur,
                  const QString& title, const QString& artist, qint64 dur)
{
    const QString wt = normTitleKey(wantTitle), ct = normTitleKey(title);
    if (wt.isEmpty() || ct.isEmpty()) return 0;
    double s = 0;
    if (wt == ct) s += 0.50;
    else if (ct.contains(wt) || wt.contains(ct)) s += 0.25;
    else return 0;
    if (variantTag(wantTitle) != variantTag(title)) return 0;          // remix / live / slowed are different recordings
    const QString wa = primaryArtistKey(wantArtist), ca = primaryArtistKey(artist);
    if (!wa.isEmpty()) {
        if (wa == ca) s += 0.35;
        else if (!ca.isEmpty() && (ca.contains(wa) || wa.contains(ca))) s += 0.20;
        else {
            // the credited artist can be spelled differently (no spaces, "The ..."); compare letters only
            QString a = wa, b = ca;
            a.remove(' '); b.remove(' ');
            if (!a.isEmpty() && !b.isEmpty() && (a == b || a.startsWith(b) || b.startsWith(a))) s += 0.25;
            else return 0;
        }
    } else s += 0.10;
    if (wantDur > 0 && dur > 0) {
        const qint64 d = std::abs(wantDur - dur);
        if (d <= 4000) s += 0.15;
        else if (d <= 10000) s += 0.08;
        else if (d > 40000) return 0;
    } else s += 0.05;
    return std::min(1.0, s);
}

QString bigCover(const QJsonObject& album)
{
    for (const char* k : {"cover_big", "cover_medium", "cover_xl", "cover"}) {
        const QString s = album.value(k).toString();
        if (!s.isEmpty()) return s;
    }
    return {};
}
} // namespace

QVariantMap SongMeta::toMap() const
{
    return {{"ok", ok}, {"confidence", confidence}, {"title", title}, {"artist", artist}, {"album", album},
            {"genre", genre}, {"isrc", isrc}, {"cover", cover}, {"source", source}, {"year", year},
            {"durationMs", durationMs}, {"artistId", artistId}, {"albumId", albumId}, {"catalogId", catalogId}};
}

Metadata::Metadata(Catalog* catalog, QObject* parent) : QObject(parent), m_catalog(catalog)
{
    const QString order = qEnvironmentVariable("LUMEN_META_PROVIDERS", QStringLiteral("deezer,itunes,musicbrainz"));
    for (const QString& p : order.split(',', Qt::SkipEmptyParts)) {
        const QString k = p.trimmed().toLower();
        if ((k == "deezer" || k == "itunes" || k == "musicbrainz") && !m_providers.contains(k)) m_providers << k;
    }
    m_itunes = qEnvironmentVariable("LUMEN_ITUNES_URL", QStringLiteral("https://itunes.apple.com"));
    m_mb = qEnvironmentVariable("LUMEN_MUSICBRAINZ_URL", QStringLiteral("https://musicbrainz.org"));
    m_contact = qEnvironmentVariable("LUMEN_CONTACT", QStringLiteral("https://github.com/lumen-player/lumen"));
    while (m_itunes.endsWith('/')) m_itunes.chop(1);
    while (m_mb.endsWith('/')) m_mb.chop(1);
}

void Metadata::request(const QString& title, const QString& artist, qint64 durationMs)
{
    lookup(title, artist, durationMs, [this, title, artist](const SongMeta& m) { emit found(title, artist, m.toMap()); });
}

void Metadata::lookup(const QString& title, const QString& artist, qint64 durationMs, Done cb)
{
    const QString key = normTitleKey(title) + QChar(0x1f) + primaryArtistKey(artist) + QChar(0x1f) + variantTag(title);
    if (normTitleKey(title).isEmpty() || m_providers.isEmpty()) {
        QTimer::singleShot(0, this, [cb] { cb(SongMeta{}); });
        return;
    }
    if (const auto it = m_memo.constFind(key); it != m_memo.constEnd()) {
        const SongMeta m = *it;
        QTimer::singleShot(0, this, [cb, m] { cb(m); });
        return;
    }
    auto job = std::make_shared<Job>();
    job->title = title; job->artist = artist; job->durationMs = durationMs; job->key = key; job->cb = std::move(cb);
    next(job);
}

void Metadata::next(std::shared_ptr<Job> job)
{
    if (job->provider >= m_providers.size()) { finish(job, job->best); return; }
    const QString p = m_providers[job->provider++];
    auto done = [this, job](SongMeta m) {
        if (m.ok && m.confidence > job->best.confidence) job->best = m;
        if (job->best.ok && job->best.confidence >= kAccept) { finish(job, job->best); return; }
        next(job);
    };
    if (p == "deezer") deezer(job, done);
    else if (p == "itunes") itunes(job, done);
    else musicbrainz(job, done);
}

void Metadata::finish(std::shared_ptr<Job> job, SongMeta m)
{
    if (!m.ok || m.confidence < kAccept) { m = SongMeta{}; }
    else m_memo.insert(job->key, m);                     // misses are not memoised: a later try may succeed
    job->cb(m);
}

// ---- HTTP: one lane per provider so a slow provider never delays another -------------------

void Metadata::fetch(const QString& lane, int gapMs, const QUrl& url, int ttlSec, std::function<void(const QJsonObject&)> cb)
{
    const QString cacheKey = url.toString();
    if (const auto hit = Db::get().cacheGet(cacheKey, ttlSec)) {
        const QByteArray body = *hit;
        QTimer::singleShot(0, this, [cb, body] { cb(QJsonDocument::fromJson(body).object()); });
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 at = std::max(now, m_laneNext.value(lane, 0));
    m_laneNext[lane] = at + gapMs;
    QTimer::singleShot(int(at - now), this, [this, url, cb, cacheKey] {
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumen/0.3 ( %1 )").arg(m_contact));
        req.setRawHeader("Accept", "application/json");
        req.setTransferTimeout(12000);
        QNetworkReply* r = m_net.get(req);
        connect(r, &QNetworkReply::finished, this, [r, cb, cacheKey] {
            r->deleteLater();
            const QByteArray body = r->readAll();
            const bool good = r->error() == QNetworkReply::NoError && !body.isEmpty();
            const QJsonObject o = good ? QJsonDocument::fromJson(body).object() : QJsonObject();
            if (good && !o.isEmpty()) Db::get().cachePut(cacheKey, body);
            cb(o);
        });
    });
}

// ---- Deezer (through the catalog's own paced, cached client) ----------------------------------

void Metadata::deezer(std::shared_ptr<Job> job, std::function<void(SongMeta)> done)
{
    const QString strict = QStringLiteral("artist:\"%1\" track:\"%2\"").arg(job->artist, normTitleKey(job->title));
    const QString loose = (job->artist + QLatin1Char(' ') + normTitleKey(job->title)).trimmed();
    const bool haveArtist = !job->artist.trimmed().isEmpty();
    auto parse = [job](const QJsonObject& root) {
        SongMeta best;
        for (const QJsonValue& v : root.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            const QJsonObject a = o.value("artist").toObject(), al = o.value("album").toObject();
            const double s = matchScore(job->title, job->artist, job->durationMs, o.value("title").toString(),
                                        a.value("name").toString(), qint64(o.value("duration").toInt()) * 1000);
            if (s <= 0 || s <= best.confidence) continue;
            best.ok = true; best.confidence = s; best.source = QStringLiteral("Deezer");
            best.title = o.value("title").toString(); best.artist = a.value("name").toString();
            best.album = al.value("title").toString(); best.cover = bigCover(al);
            best.durationMs = qint64(o.value("duration").toInt()) * 1000;
            best.catalogId = Catalog::idOf(o.value("id")); best.artistId = Catalog::idOf(a.value("id"));
            best.albumId = al.contains("id") ? Catalog::idOf(al.value("id")) : QString();
        }
        return best;
    };
    // details (ISRC, release date, genre) cost two more cached requests, only for a confident hit
    auto details = [this, done](SongMeta m) {
        if (m.catalogId.isEmpty()) { done(m); return; }
        m_catalog->get("/track/" + m.catalogId, kTtl, [this, m, done](const QJsonObject& t, const QString& err) mutable {
            if (err.isEmpty()) {
                m.isrc = t.value("isrc").toString();
                m.year = t.value("release_date").toString().left(4).toInt();
            }
            if (m.albumId.isEmpty()) { done(m); return; }
            m_catalog->get("/album/" + m.albumId, kTtl, [m, done](const QJsonObject& al, const QString& e2) mutable {
                if (e2.isEmpty()) {
                    const QJsonArray g = al.value("genres").toObject().value("data").toArray();
                    if (!g.isEmpty()) m.genre = g.first().toObject().value("name").toString();
                    if (!m.year) m.year = al.value("release_date").toString().left(4).toInt();
                }
                done(m);
            });
        });
    };
    // 3rd try: the title alone. A channel name like "JuiceWRLDVEVO" ruins the two queries above, but
    // matchScore still insists on an artist that is close, so a wrong song cannot sneak in.
    auto third = [this, job, parse, details, done](SongMeta prev) {
        if (prev.ok && prev.confidence >= kAccept) { details(prev); return; }
        m_catalog->get("/search?limit=15&q=" + QString::fromLatin1(QUrl::toPercentEncoding(normTitleKey(job->title))), kTtl,
                       [parse, details, done, prev](const QJsonObject& root, const QString&) {
                           SongMeta m = parse(root);
                           if (prev.confidence > m.confidence) m = prev;
                           if (m.ok && m.confidence >= kAccept) details(m); else done(m);
                       });
    };
    auto second = [this, loose, parse, third](SongMeta first) {
        if (first.ok && first.confidence >= kAccept) { third(first); return; }
        m_catalog->get("/search?limit=8&q=" + QString::fromLatin1(QUrl::toPercentEncoding(loose)), kTtl,
                       [parse, third, first](const QJsonObject& root, const QString&) {
                           SongMeta m = parse(root);
                           if (first.confidence > m.confidence) m = first;
                           third(m);
                       });
    };
    if (!haveArtist) { second(SongMeta{}); return; }
    m_catalog->get("/search?limit=6&q=" + QString::fromLatin1(QUrl::toPercentEncoding(strict)), kTtl,
                   [parse, second](const QJsonObject& root, const QString&) { second(parse(root)); });
}

// ---- iTunes ---------------------------------------------------------------------------------

void Metadata::itunes(std::shared_ptr<Job> job, std::function<void(SongMeta)> done)
{
    QUrl url(m_itunes + "/search");
    QUrlQuery q;
    q.addQueryItem("term", (job->artist + QLatin1Char(' ') + normTitleKey(job->title)).trimmed());
    q.addQueryItem("media", "music");
    q.addQueryItem("entity", "song");
    q.addQueryItem("limit", "8");
    url.setQuery(q);
    fetch(QStringLiteral("itunes"), 3200, url, kTtl, [job, done](const QJsonObject& root) {
        SongMeta best;
        for (const QJsonValue& v : root.value("results").toArray()) {
            const QJsonObject o = v.toObject();
            const double s = matchScore(job->title, job->artist, job->durationMs, o.value("trackName").toString(),
                                        o.value("artistName").toString(), qint64(o.value("trackTimeMillis").toDouble()));
            if (s <= 0 || s <= best.confidence) continue;
            best.ok = true; best.confidence = s; best.source = QStringLiteral("Apple Music");
            best.title = o.value("trackName").toString(); best.artist = o.value("artistName").toString();
            best.album = o.value("collectionName").toString(); best.genre = o.value("primaryGenreName").toString();
            best.year = o.value("releaseDate").toString().left(4).toInt();
            best.durationMs = qint64(o.value("trackTimeMillis").toDouble());
            QString art = o.value("artworkUrl100").toString();
            art.replace(QStringLiteral("100x100"), QStringLiteral("600x600"));
            best.cover = art;
        }
        done(best);
    });
}

// ---- MusicBrainz -------------------------------------------------------------------------------

void Metadata::musicbrainz(std::shared_ptr<Job> job, std::function<void(SongMeta)> done)
{
    auto esc = [](QString s) { s.replace('\\', "\\\\"); s.replace('"', "\\\""); return s; };
    QString query = QStringLiteral("recording:\"%1\"").arg(esc(normTitleKey(job->title)));
    if (!job->artist.trimmed().isEmpty()) query += QStringLiteral(" AND artist:\"%1\"").arg(esc(primaryArtistKey(job->artist)));
    QUrl url(m_mb + "/ws/2/recording/");
    QUrlQuery q;
    q.addQueryItem("query", query);
    q.addQueryItem("fmt", "json");
    q.addQueryItem("limit", "6");
    url.setQuery(q);
    fetch(QStringLiteral("musicbrainz"), 1100, url, kTtl, [job, done](const QJsonObject& root) {
        SongMeta best;
        for (const QJsonValue& v : root.value("recordings").toArray()) {
            const QJsonObject o = v.toObject();
            QString artist;
            for (const QJsonValue& c : o.value("artist-credit").toArray()) {
                const QJsonObject co = c.toObject();
                if (artist.isEmpty()) artist = co.value("name").toString();
            }
            const double s = matchScore(job->title, job->artist, job->durationMs, o.value("title").toString(), artist,
                                        qint64(o.value("length").toDouble()));
            if (s <= 0 || s <= best.confidence) continue;
            best.ok = true; best.confidence = s; best.source = QStringLiteral("MusicBrainz");
            best.title = o.value("title").toString(); best.artist = artist;
            best.durationMs = qint64(o.value("length").toDouble());
            const QJsonArray rel = o.value("releases").toArray();
            if (!rel.isEmpty()) {
                best.album = rel.first().toObject().value("title").toString();
                best.year = rel.first().toObject().value("date").toString().left(4).toInt();
            }
            const QJsonArray isrcs = o.value("isrcs").toArray();
            if (!isrcs.isEmpty()) best.isrc = isrcs.first().toString();
            const QJsonArray tags = o.value("tags").toArray();
            if (!tags.isEmpty()) best.genre = tags.first().toObject().value("name").toString();
        }
        done(best);
    });
}
