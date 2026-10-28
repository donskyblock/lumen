#include "Catalog.h"
#include "TextUtil.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <memory>

namespace {
constexpr int kMaxInflight = 4;
constexpr int kMaxPer5s = 40;           // Deezer allows 50 requests / 5 s per IP

QString pickCover(const QJsonObject& o)
{
    for (const char* k : {"cover_big", "cover_medium", "cover_xl", "cover"}) {
        const QString s = o.value(k).toString();
        if (!s.isEmpty()) return s;
    }
    return {};
}
QString pickPicture(const QJsonObject& o)
{
    for (const char* k : {"picture_big", "picture_medium", "picture_xl", "picture"}) {
        const QString s = o.value(k).toString();
        if (!s.isEmpty()) return s;
    }
    return {};
}
int typeRank(const QString& t) { return t == "album" ? 3 : t == "ep" ? 2 : t == "single" ? 1 : 0; }
bool preferredFlag(bool candidate, bool current, bool preferExplicit)
{
    return candidate != current && candidate == preferExplicit;
}

QVector<QJsonObject> chooseAlbumVersions(const QJsonArray& input, bool preferExplicit)
{
    QVector<QJsonObject> albums;
    for (const QJsonValue& v : input) albums.push_back(v.toObject());
    auto key = [](const QJsonObject& o) {
        const QJsonObject artist = o.value("artist").toObject();
        const QString artistId = artist.value("id").toVariant().toString();
        return foldText(o.value("title").toString()) + QChar(0x1f) + artistId + QChar(0x1f) + o.value("record_type").toString();
    };
    std::stable_sort(albums.begin(), albums.end(), [preferExplicit](const QJsonObject& a, const QJsonObject& b) {
        return preferredFlag(a.value("explicit_lyrics").toBool(), b.value("explicit_lyrics").toBool(), preferExplicit);
    });
    QSet<QString> seen;
    QVector<QJsonObject> out;
    for (const QJsonObject& album : std::as_const(albums)) {
        const QString k = key(album);
        if (k.endsWith(QChar(0x1f))) { out.push_back(album); continue; }
        if (!seen.contains(k)) { seen.insert(k); out.push_back(album); }
    }
    return out;
}

// Walks pages of a list endpoint one after another.
struct Pager : std::enable_shared_from_this<Pager> {
    Catalog* c = nullptr;
    QString path;
    int ttl = 0, max = 0;
    QJsonArray acc;
    std::function<void(const QJsonArray&, const QString&)> done;
    void next(int index)
    {
        auto self = shared_from_this();
        const QString sep = path.contains('?') ? QStringLiteral("&") : QStringLiteral("?");
        c->get(path + sep + QStringLiteral("limit=100&index=") + QString::number(index), ttl, [self, index](const QJsonObject& root, const QString& err) {
            if (!err.isEmpty()) { self->done(self->acc, self->acc.isEmpty() ? err : QString()); return; }
            const QJsonArray page = root.value("data").toArray();
            for (const QJsonValue& v : page) self->acc.append(v);
            const int total = root.value("total").toInt(-1);
            const int got = index + page.size();
            if (page.size() < 100 || self->acc.size() >= self->max || (total >= 0 && got >= total)) { self->done(self->acc, QString()); return; }
            self->next(index + 100);
        });
    }
};
} // namespace

Catalog::Catalog(Library* lib, QObject* parent) : QObject(parent), m_lib(lib)
{
    m_preferExplicit = Db::get().kv("catalog.preferExplicit", "1") == "1";
    m_base = qEnvironmentVariable("LUMEN_DEEZER_URL", QStringLiteral("https://api.deezer.com"));
    while (m_base.endsWith('/')) m_base.chop(1);
    m_pump.setSingleShot(true);
    connect(&m_pump, &QTimer::timeout, this, &Catalog::pump);
    Db::get().cachePrune(14 * 86400);
}

void Catalog::setPreferExplicit(bool on)
{
    if (on == m_preferExplicit) return;
    m_preferExplicit = on;
    Db::get().setKv("catalog.preferExplicit", on ? "1" : "0");
    emit preferExplicitChanged();
}

void Catalog::setOnline(bool on)
{
    if (on == m_online) return;
    m_online = on;
    emit onlineChanged();
}

// ---- HTTP: cache, queue, pacing, retry -------------------------------------------------------

void Catalog::get(const QString& path, int ttl, Reply cb, int tries)
{
    const QString key = m_base + path;
    if (ttl > 0) {
        if (const auto hit = Db::get().cacheGet(key, ttl)) {
            const QByteArray body = *hit;
            QTimer::singleShot(0, this, [cb, body] {
                cb(QJsonDocument::fromJson(body).object(), QString());
            });
            return;
        }
    }
    m_queue.push_back({path, ttl, std::move(cb), tries});
    pump();
}

void Catalog::pump()
{
    while (m_inflight < kMaxInflight && !m_queue.empty()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        while (!m_sent.empty() && now - m_sent.front() > 5000) m_sent.pop_front();
        if (int(m_sent.size()) >= kMaxPer5s) {
            m_pump.start(int(std::max<qint64>(50, 5000 - (now - m_sent.front()) + 25)));
            return;
        }
        Pending p = std::move(m_queue.front());
        m_queue.pop_front();
        m_sent.push_back(now);
        send(std::move(p));
    }
}

void Catalog::send(Pending p)
{
    ++m_inflight;
    QNetworkRequest req{QUrl(m_base + p.path)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumen/0.2"));
    req.setTransferTimeout(15000);
    QNetworkReply* r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, p = std::move(p)]() mutable {
        r->deleteLater();
        --m_inflight;
        const QByteArray body = r->readAll();
        const bool netFail = r->error() != QNetworkReply::NoError && body.isEmpty();
        if (netFail) {
            setOnline(false);
            p.cb({}, tr("Can't reach the music catalog (%1).").arg(r->errorString()));
            pump();
            return;
        }
        setOnline(true);
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        if (root.contains("error")) {
            const QJsonObject e = root.value("error").toObject();
            if (e.value("code").toInt() == 4 && p.tries < 4) {            // quota: wait and go again
                p.tries++;
                QTimer::singleShot(1500, this, [this, p = std::move(p)]() mutable { m_queue.push_front(std::move(p)); pump(); });
                pump();
                return;
            }
            p.cb({}, e.value("message").toString(tr("The catalog returned an error.")));
            pump();
            return;
        }
        if (p.ttl > 0) Db::get().cachePut(m_base + p.path, body);
        p.cb(root, QString());
        pump();
    });
}

void Catalog::getAll(const QString& path, int ttl, int maxItems, std::function<void(const QJsonArray&, const QString&)> cb)
{
    auto pg = std::make_shared<Pager>();
    pg->c = this; pg->path = path; pg->ttl = ttl; pg->max = maxItems; pg->done = std::move(cb);
    pg->next(0);
}

// ---- conversion -------------------------------------------------------------------------------------

Track Catalog::toTrack(const QJsonObject& o, const QJsonObject& albumIn) const
{
    Track t;
    const QJsonObject album = albumIn.isEmpty() ? o.value("album").toObject() : albumIn;
    const QJsonObject artist = o.value("artist").toObject();
    t.path = catalogPath(idOf(o.value("id")));
    t.title = o.value("title").toString();
    t.artist = artist.value("name").toString();
    t.artistId = idOf(artist.value("id"));
    t.album = album.value("title").toString();
    t.albumId = album.contains("id") ? idOf(album.value("id")) : QString();
    const QJsonObject albumArtist = album.value("artist").toObject();
    t.albumArtist = albumArtist.contains("name") ? albumArtist.value("name").toString() : t.artist;
    t.coverUrl = pickCover(album);
    t.coverKey = t.coverUrl;
    t.durationMs = qint64(o.value("duration").toInt()) * 1000;
    t.trackNo = o.value("track_position").toInt();
    t.discNo = o.value("disk_number").toInt();
    t.rank = o.value("rank").toInt();
    t.explicitLyrics = o.contains("explicit_lyrics") ? o.value("explicit_lyrics").toBool()
                                                       : album.value("explicit_lyrics").toBool();
    t.year = album.value("release_date").toString().left(4).toInt();
    t.codec = QStringLiteral("Stream");
    t.added = QDateTime::currentSecsSinceEpoch();
    return t;
}

QVariantMap Catalog::trackMap(const Track& t) const
{
    return {{"path", t.path}, {"id", catalogId(t.path)}, {"title", t.title}, {"artist", t.artist}, {"artistId", t.artistId},
            {"album", t.album}, {"albumId", t.albumId}, {"trackNo", t.trackNo}, {"durationMs", t.durationMs},
            {"quality", QString()}, {"lossless", false}, {"cover", t.coverKey}, {"rank", t.rank}, {"year", t.year},
            {"explicit", t.explicitLyrics}, {"saved", m_lib->isSaved(t.path)}};
}

QVector<Track> Catalog::chooseTrackVersions(const QVector<Track>& tracks) const
{
    QVector<Track> out;
    QHash<QString, int> seen;
    for (const Track& track : tracks) {
        const QString k = normTitleKey(track.title) + QChar(0x1f) + variantTag(track.title) + QChar(0x1f)
                        + (track.artistId.isEmpty() ? primaryArtistKey(track.artist) : track.artistId);
        const auto it = seen.constFind(k);
        if (k.startsWith(QChar(0x1f)) || it == seen.constEnd()) {
            seen.insert(k, out.size());
            out.push_back(track);
            continue;
        }
        Track& current = out[*it];
        if (preferredFlag(track.explicitLyrics, current.explicitLyrics, m_preferExplicit)
            || (track.explicitLyrics == current.explicitLyrics && track.rank > current.rank)) current = track;
    }
    return out;
}

QVariantMap Catalog::artistMap(const QJsonObject& o) const
{
    return {{"id", idOf(o.value("id"))}, {"name", o.value("name").toString()}, {"picture", pickPicture(o)},
            {"fans", o.value("nb_fan").toVariant().toLongLong()}, {"albumCount", o.value("nb_album").toInt()}};
}

QVariantMap Catalog::albumMap(const QJsonObject& o, const QString& artistName) const
{
    const QJsonObject a = o.value("artist").toObject();
    return {{"id", idOf(o.value("id"))}, {"title", o.value("title").toString()}, {"cover", pickCover(o)},
            {"year", o.value("release_date").toString().left(4).toInt()}, {"type", o.value("record_type").toString()},
            {"explicit", o.value("explicit_lyrics").toBool()}, {"artist", a.contains("name") ? a.value("name").toString() : artistName},
            {"artistId", a.contains("id") ? idOf(a.value("id")) : QString()}};
}

QVector<Track> Catalog::remember(const QVector<Track>& t)
{
    m_lib->noteRemote(t);
    return t;
}

// ---- search -------------------------------------------------------------------------------------------

int Catalog::search(const QString& query)
{
    const int gen = ++m_searchGen;
    const QString q = query.trimmed();
    if (q.size() < 2) { emit searchDone(gen, {}); return gen; }
    struct State { QJsonArray artists, tracks, albums; int left = 3; QString err; };
    auto st = std::make_shared<State>();
    const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(q));
    auto publish = [this, st, gen] {
        if (gen != m_searchGen) return;
        QVariantList artists, albums, tracks;
        QVariantList songArtists, nameArtists;
        QVector<Track> ts;
        QSet<QString> artistIds;
        for (const QJsonObject& o : chooseAlbumVersions(st->albums, m_preferExplicit)) albums << albumMap(o);
        for (const QJsonValue& v : std::as_const(st->tracks)) {
            const QJsonObject o = v.toObject();
            ts.push_back(toTrack(o));
            // A song search such as "Lucid Dreams" normally has no direct artist
            // search hit.  Surface the recording's credited artist alongside the
            // song, so the artist shelf is useful for song-first searches too.
            const QJsonObject a = o.value("artist").toObject();
            const QString id = a.contains("id") ? idOf(a.value("id")) : QString();
            if (!id.isEmpty() && !artistIds.contains(id)) {
                QVariantMap m = artistMap(a);
                if (m.value("picture").toString().isEmpty()) m["picture"] = pickCover(o.value("album").toObject());
                songArtists << m;                 // song relevance beats a loose artist-name match
                artistIds.insert(id);
                Db::get().upsertCatArtist({id, a.value("name").toString(), m.value("picture").toString(), 0, false}, false);
            }
        }
        ts = chooseTrackVersions(ts);
        for (const QJsonValue& v : std::as_const(st->artists)) {
            const QJsonObject o = v.toObject();
            const QString id = o.contains("id") ? idOf(o.value("id")) : QString();
            if (id.isEmpty() || artistIds.contains(id)) continue;
            nameArtists << artistMap(o);
            artistIds.insert(id);
            Db::get().upsertCatArtist({id, o.value("name").toString(), pickPicture(o), o.value("nb_fan").toVariant().toLongLong(), false}, false);
        }
        artists = songArtists;
        artists += nameArtists;
        remember(ts);
        for (const Track& t : std::as_const(ts)) tracks << trackMap(t);
        QVariantMap res{{"artists", artists}, {"albums", albums}, {"tracks", tracks}, {"error", st->err}};
        emit searchDone(gen, res);
    };
    auto finish = [st, publish] { if (--st->left == 0) publish(); };
    get("/search/artist?limit=8&q=" + enc, 600, [st, finish](const QJsonObject& r, const QString& e) { st->artists = r.value("data").toArray(); if (!e.isEmpty()) st->err = e; finish(); });
    get("/search?limit=25&q=" + enc, 600, [st, publish, finish](const QJsonObject& r, const QString& e) {
        st->tracks = r.value("data").toArray();
        if (!e.isEmpty()) st->err = e;
        publish();                              // songs and their artists appear as soon as this fastest result arrives
        finish();
    });
    get("/search/album?limit=12&q=" + enc, 600, [st, finish](const QJsonObject& r, const QString& e) { st->albums = r.value("data").toArray(); if (!e.isEmpty()) st->err = e; finish(); });
    return gen;
}

// ---- artist page ---------------------------------------------------------------------------------------------

void Catalog::loadArtist(const QString& id)
{
    struct State { QJsonObject info; QJsonArray top, albums, related; QString err; int left = 4; };
    auto st = std::make_shared<State>();
    auto finish = [this, st, id] {
        if (--st->left > 0) return;
        if (st->info.isEmpty()) { emit loadFailed("artist", id, st->err.isEmpty() ? tr("Artist not found.") : st->err); return; }
        QVariantMap page = artistMap(st->info);
        const QString name = page.value("name").toString();
        Db::get().upsertCatArtist({id, name, page.value("picture").toString(), page.value("fans").toLongLong(), false}, false);

        QVector<Track> popular;
        for (const QJsonValue& v : std::as_const(st->top)) popular.push_back(toTrack(v.toObject()));
        popular = chooseTrackVersions(popular);
        remember(popular);
        QVariantList pop;
        for (const Track& t : std::as_const(popular)) pop << trackMap(t);

        struct A { QVariantMap m; QString date; int type; };
        QVector<A> all;
        QSet<QString> seen;
        for (const QJsonObject& o : chooseAlbumVersions(st->albums, m_preferExplicit)) {
            const QVariantMap m = albumMap(o, name);
            const QString type = m.value("type").toString();
            const QString key = foldText(m.value("title").toString()) + QChar(0x1f) + type;
            if (seen.contains(key)) continue;                       // clean/explicit duplicates
            seen.insert(key);
            all.push_back({m, o.value("release_date").toString(), typeRank(type)});
        }
        std::sort(all.begin(), all.end(), [](const A& a, const A& b) { return a.date > b.date; });
        QVariantList albums, singles, compilations;
        for (const A& a : std::as_const(all)) {
            if (a.type == 3) albums << a.m;
            else if (a.type >= 1) singles << a.m;
            else compilations << a.m;
        }
        QVariantList related;
        for (const QJsonValue& v : std::as_const(st->related)) {
            const QJsonObject o = v.toObject();
            related << artistMap(o);
            Db::get().upsertCatArtist({idOf(o.value("id")), o.value("name").toString(), pickPicture(o), o.value("nb_fan").toVariant().toLongLong(), false}, false);
        }
        page["popular"] = pop;
        page["albums"] = albums;
        page["singles"] = singles;
        page["compilations"] = compilations;
        page["related"] = related;
        page["saved"] = isSavedArtist(id);
        m_artistPages.insert(id, page);
        m_pageOrder << "a" + id;
        while (m_pageOrder.size() > 24) { const QString k = m_pageOrder.takeFirst(); if (k[0] == 'a') m_artistPages.remove(k.mid(1)); else m_albumPages.remove(k.mid(1)); }
        emit artistLoaded(id, page);
    };
    get("/artist/" + id, 86400, [st, finish](const QJsonObject& r, const QString& e) { st->info = r; if (!e.isEmpty()) st->err = e; finish(); });
    get("/artist/" + id + "/top?limit=50", 21600, [st, finish](const QJsonObject& r, const QString&) { st->top = r.value("data").toArray(); finish(); });
    get("/artist/" + id + "/related?limit=14", 604800, [st, finish](const QJsonObject& r, const QString&) { st->related = r.value("data").toArray(); finish(); });
    getAll("/artist/" + id + "/albums", 86400, 400, [st, finish](const QJsonArray& a, const QString&) { st->albums = a; finish(); });
}

void Catalog::loadAlbum(const QString& id)
{
    struct State { QJsonObject info; QJsonArray tracks; QString err; int left = 2; };
    auto st = std::make_shared<State>();
    auto finish = [this, st, id] {
        if (--st->left > 0) return;
        if (st->info.isEmpty()) { emit loadFailed("album", id, st->err.isEmpty() ? tr("Album not found.") : st->err); return; }
        QVariantMap page = albumMap(st->info);
        QVector<Track> ts;
        for (const QJsonValue& v : std::as_const(st->tracks)) {
            QJsonObject o = v.toObject();
            ts.push_back(toTrack(o, st->info));
        }
        std::stable_sort(ts.begin(), ts.end(), [](const Track& a, const Track& b) {
            if (a.discNo != b.discNo) return a.discNo < b.discNo;
            return a.trackNo < b.trackNo;
        });
        remember(ts);
        QVariantList list;
        qint64 dur = 0;
        for (const Track& t : std::as_const(ts)) { list << trackMap(t); dur += t.durationMs; }
        page["tracks"] = list;
        page["durationMs"] = dur;
        page["trackCount"] = list.size();
        page["label"] = st->info.value("label").toString();
        page["genres"] = [&] { QStringList g; for (const QJsonValue& v : st->info.value("genres").toObject().value("data").toArray()) g << v.toObject().value("name").toString(); return g; }();
        m_albumPages.insert(id, page);
        m_pageOrder << "b" + id;
        emit albumLoaded(id, page);
    };
    get("/album/" + id, 86400, [st, finish](const QJsonObject& r, const QString& e) { st->info = r; if (!e.isEmpty()) st->err = e; finish(); });
    getAll("/album/" + id + "/tracks", 86400, 300, [st, finish](const QJsonArray& a, const QString&) { st->tracks = a; finish(); });
}

void Catalog::loadCharts()
{
    struct State { QJsonArray artists, tracks, albums; int left = 3; };
    auto st = std::make_shared<State>();
    auto finish = [this, st] {
        if (--st->left > 0) return;
        QVariantList artists, tracks, albums;
        QVector<Track> ts;
        for (const QJsonValue& v : std::as_const(st->artists)) {
            const QJsonObject o = v.toObject();
            artists << artistMap(o);
            Db::get().upsertCatArtist({idOf(o.value("id")), o.value("name").toString(), pickPicture(o), 0, false}, false);
        }
        for (const QJsonObject& o : chooseAlbumVersions(st->albums, m_preferExplicit)) albums << albumMap(o);
        for (const QJsonValue& v : std::as_const(st->tracks)) ts.push_back(toTrack(v.toObject()));
        ts = chooseTrackVersions(ts);
        remember(ts);
        for (const Track& t : std::as_const(ts)) tracks << trackMap(t);
        if (artists.isEmpty() && tracks.isEmpty()) return;
        m_charts = {{"artists", artists}, {"tracks", tracks}, {"albums", albums}};
        emit chartsChanged();
    };
    get("/chart/0/artists?limit=24", 21600, [st, finish](const QJsonObject& r, const QString&) { st->artists = r.value("data").toArray(); finish(); });
    get("/chart/0/tracks?limit=30", 21600, [st, finish](const QJsonObject& r, const QString&) { st->tracks = r.value("data").toArray(); finish(); });
    get("/chart/0/albums?limit=20", 21600, [st, finish](const QJsonObject& r, const QString&) { st->albums = r.value("data").toArray(); finish(); });
}

QStringList Catalog::artistPaths(const QString& id) const
{
    QStringList out;
    for (const QVariant& v : m_artistPages.value(id).value("popular").toList()) out << v.toMap().value("path").toString();
    return out;
}

QStringList Catalog::albumPaths(const QString& id) const
{
    QStringList out;
    for (const QVariant& v : m_albumPages.value(id).value("tracks").toList()) out << v.toMap().value("path").toString();
    return out;
}

bool Catalog::isSavedArtist(const QString& id) const
{
    const auto a = Db::get().catArtist(id);
    return a && a->saved;
}

// ---- helpers used by the resolver and the algorithm ----------------------------------------------------------

void Catalog::freshPreview(const QString& id, std::function<void(const QString&, qint64, const QString&)> cb)
{
    get("/track/" + id, 0, [cb](const QJsonObject& r, const QString& err) {
        if (!err.isEmpty()) { cb({}, 0, err); return; }
        const QString url = r.value("preview").toString();
        cb(url, qint64(r.value("duration").toInt()) * 1000, url.isEmpty() ? QObject::tr("This song has no preview available.") : QString());
    });
}

void Catalog::resolveArtist(const QString& name, std::function<void(const std::optional<Db::CatArtist>&)> cb)
{
    if (const auto known = Db::get().catArtistByName(name)) {
        QTimer::singleShot(0, this, [cb, known] { cb(known); });
        return;
    }
    const QString want = foldText(name).simplified();
    get("/search/artist?limit=5&q=" + QString::fromLatin1(QUrl::toPercentEncoding(name)), 604800, [this, cb, want](const QJsonObject& r, const QString&) {
        std::optional<Db::CatArtist> best;
        for (const QJsonValue& v : r.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            Db::CatArtist a{idOf(o.value("id")), o.value("name").toString(), pickPicture(o), o.value("nb_fan").toVariant().toLongLong(), false};
            Db::get().upsertCatArtist(a, false);
            if (foldText(a.name).simplified() == want && (!best || a.fans > best->fans)) best = a;
        }
        cb(best);
    });
}

void Catalog::relatedArtists(const QString& id, std::function<void(const QVector<Db::CatArtist>&)> cb)
{
    get("/artist/" + id + "/related?limit=20", 604800, [cb](const QJsonObject& r, const QString&) {
        QVector<Db::CatArtist> out;
        for (const QJsonValue& v : r.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            Db::CatArtist a{idOf(o.value("id")), o.value("name").toString(), pickPicture(o), o.value("nb_fan").toVariant().toLongLong(), false};
            Db::get().upsertCatArtist(a, false);
            out.push_back(a);
        }
        cb(out);
    });
}

void Catalog::topTracks(const QString& id, std::function<void(const QVector<Track>&)> cb)
{
    get("/artist/" + id + "/top?limit=50", 86400, [this, cb](const QJsonObject& r, const QString&) {
        QVector<Track> out;
        for (const QJsonValue& v : r.value("data").toArray()) out.push_back(toTrack(v.toObject()));
        out = chooseTrackVersions(out);
        remember(out);
        cb(out);
    });
}

void Catalog::findTrack(const QString& artist, const QString& title, std::function<void(const std::optional<Track>&)> cb)
{
    const QString q = QStringLiteral("artist:\"%1\" track:\"%2\"").arg(artist, title);
    const QString wantTitle = normTitleKey(title), wantArtist = primaryArtistKey(artist);
    get("/search?limit=8&q=" + QString::fromLatin1(QUrl::toPercentEncoding(q)), 604800, [this, cb, wantTitle, wantArtist](const QJsonObject& r, const QString&) {
        std::optional<Track> best;
        for (const QJsonValue& v : r.value("data").toArray()) {
            const Track t = toTrack(v.toObject());
            if (normTitleKey(t.title) != wantTitle || primaryArtistKey(t.artist) != wantArtist) continue;
            if (!best || preferredFlag(t.explicitLyrics, best->explicitLyrics, m_preferExplicit)
                || (t.explicitLyrics == best->explicitLyrics && t.rank > best->rank)) best = t;
        }
        if (best) remember({*best});
        cb(best);
    });
}

void Catalog::matchImportedTrack(const QString& title, const QString& artist, const QString& album, qint64 durationMs,
                                 std::function<void(const std::optional<Track>&)> cb)
{
    QString query;
    if (!artist.trimmed().isEmpty()) query = QStringLiteral("artist:\"%1\" ").arg(artist.trimmed());
    query += QStringLiteral("track:\"%1\"").arg(title.trimmed());
    get("/search?limit=25&q=" + QString::fromLatin1(QUrl::toPercentEncoding(query)), 604800,
        [this, cb = std::move(cb), title, artist, album, durationMs](const QJsonObject& r, const QString&) {
        const QString wantedTitle = normTitleKey(title);
        const QString wantedArtist = primaryArtistKey(artist);
        const QString wantedAlbum = foldText(album).simplified();
        if (wantedTitle.isEmpty()) { cb(std::nullopt); return; }
        std::optional<Track> best;
        double bestScore = -1.0;
        for (const QJsonValue& value : r.value("data").toArray()) {
            const Track candidate = toTrack(value.toObject());
            const QString candidateTitle = normTitleKey(candidate.title);
            const int titleLength = int(std::max(wantedTitle.size(), candidateTitle.size()));
            const int titleDistance = boundedEditDistance(wantedTitle, candidateTitle, std::max(1, titleLength / 7));
            const double titleScore = titleLength ? 1.0 - double(titleDistance) / titleLength : 0.0;
            if (titleDistance > std::max(1, titleLength / 7) || titleScore < 0.82) continue;

            const QString candidateArtist = primaryArtistKey(candidate.artist);
            double artistScore = 1.0;
            if (!wantedArtist.isEmpty()) {
                if (wantedArtist == candidateArtist) artistScore = 1.0;
                else if (wantedArtist.contains(candidateArtist) || candidateArtist.contains(wantedArtist)) artistScore = 0.82;
                else {
                    const int length = std::max(wantedArtist.size(), candidateArtist.size());
                    const int distance = boundedEditDistance(wantedArtist, candidateArtist, std::max(1, int(length / 5)));
                    artistScore = length ? 1.0 - double(distance) / length : 0.0;
                }
                if (artistScore < 0.78) continue;
            }

            double score = titleScore * 0.62 + artistScore * 0.30;
            if (!wantedAlbum.isEmpty()) {
                const QString candidateAlbum = foldText(candidate.album).simplified();
                if (candidateAlbum == wantedAlbum) score += 0.06;
                else if (candidateAlbum.contains(wantedAlbum) || wantedAlbum.contains(candidateAlbum)) score += 0.035;
            }
            if (durationMs > 0 && candidate.durationMs > 0) {
                const qint64 delta = std::abs(durationMs - candidate.durationMs);
                if (delta > std::max<qint64>(20000, durationMs / 5)) continue;
                score += 0.02 * (1.0 - std::min(1.0, double(delta) / 20000.0));
            }
            if (score > bestScore || (score == bestScore && best && preferredFlag(candidate.explicitLyrics, best->explicitLyrics, m_preferExplicit))
                || (score == bestScore && best && candidate.explicitLyrics == best->explicitLyrics && candidate.rank > best->rank)) {
                best = candidate;
                bestScore = score;
            }
        }
        if (best) remember({*best});
        cb(best);
    });
}

// ---- import: "Add to library" -----------------------------------------------------------------------------------

void Catalog::setImport(const QString& id, const QString& label, double p)
{
    m_importId = id; m_importLabel = label; m_importProgress = p;
    emit importChanged();
}

void Catalog::finishImport(const QString& what, int count)
{
    setImport({}, {}, 0);
    ++m_savedRev;
    emit savedChanged();
    emit message(count > 0 ? tr("Added %1 songs from %2 to your library").arg(count).arg(what) : tr("Nothing new to add from %1").arg(what));
}

// Gathers every song credited to an artist, across all releases, picking one version of each.
void Catalog::collectArtistTracks(const QString& artistId, std::function<void(const Db::CatArtist&, const QVector<Track>&, const QString&)> done,
                                  std::function<void(double)> progress)
{
    get("/artist/" + artistId, 86400, [this, artistId, done, progress](const QJsonObject& info, const QString& err) {
        if (!err.isEmpty() || info.isEmpty()) { done({}, {}, err.isEmpty() ? tr("Artist not found.") : err); return; }
        const Db::CatArtist artist{artistId, info.value("name").toString(), pickPicture(info), info.value("nb_fan").toVariant().toLongLong(), true};
        getAll("/artist/" + artistId + "/albums", 86400, 400, [this, artist, done, progress](const QJsonArray& albums, const QString& aerr) {
            if (albums.isEmpty()) { done(artist, {}, aerr); return; }
            struct Cand { Track t; int type; };
            struct State { QHash<QString, Cand> best; int left = 0, total = 0; };
            auto st = std::make_shared<State>();
            QVector<QJsonObject> use;
            for (const QJsonObject& o : chooseAlbumVersions(albums, m_preferExplicit)) use.push_back(o);
            st->left = st->total = int(use.size());
            for (const QJsonObject& al : std::as_const(use)) {
                const int type = typeRank(al.value("record_type").toString());
                getAll("/album/" + idOf(al.value("id")) + "/tracks", 86400, 300, [this, st, al, type, artist, done, progress](const QJsonArray& tracks, const QString&) {
                    for (const QJsonValue& v : tracks) {
                        Track t = toTrack(v.toObject(), al);
                        // keep songs that are by this artist (not other people's compilation filler)
                        if (t.artistId != artist.id && !foldText(t.title).contains(foldText(artist.name))) {
                            if (idOf(al.value("artist").toObject().value("id")) != artist.id) continue;
                        }
                        const QString key = normTitleKey(t.title) + QChar(0x1f) + variantTag(t.title) + QChar(0x1f) + t.artistId;
                        auto it = st->best.find(key);
                        if (it == st->best.end()) st->best.insert(key, {t, type});
                        else if (preferredFlag(t.explicitLyrics, it->t.explicitLyrics, m_preferExplicit)
                                 || (t.explicitLyrics == it->t.explicitLyrics
                                     && (type > it->type || (type == it->type && t.rank > it->t.rank)))) *it = {t, type};
                    }
                    if (progress) progress(1.0 - double(st->left - 1) / std::max(1, st->total));
                    if (--st->left > 0) return;
                    QVector<Track> out;
                    out.reserve(st->best.size());
                    for (const Cand& c : std::as_const(st->best)) out.push_back(c.t);
                    done(artist, out, QString());
                });
            }
        });
    });
}

void Catalog::importArtist(const QString& id)
{
    if (!m_importId.isEmpty()) { emit message(tr("Another import is still running.")); return; }
    const QString shown = Db::get().catArtist(id) ? Db::get().catArtist(id)->name : tr("this artist");
    setImport(id, shown, 0.02);
    collectArtistTracks(id, [this, id](const Db::CatArtist& a, const QVector<Track>& tracks, const QString& err) {
        if (tracks.isEmpty()) {
            setImport({}, {}, 0);
            emit message(err.isEmpty() ? tr("No songs found for that artist.") : err);
            return;
        }
        Db::get().upsertCatArtist(a, true);
        m_lib->importCatalog(tracks);
        m_artistPages.remove(id);
        finishImport(a.name, int(tracks.size()));
    }, [this](double p) { if (!m_importId.isEmpty() && p > m_importProgress) { m_importProgress = p; emit importChanged(); } });
}

void Catalog::importArtistQuiet(const QString& id, std::function<void(int)> done)
{
    collectArtistTracks(id, [this, id, done](const Db::CatArtist& a, const QVector<Track>& tracks, const QString&) {
        if (!tracks.isEmpty()) {
            Db::get().upsertCatArtist(a, true);
            m_lib->importCatalog(tracks);
            ++m_savedRev;
            emit savedChanged();
        }
        done(int(tracks.size()));
    }, nullptr);
}

void Catalog::importAlbum(const QString& id)
{
    get("/album/" + id, 86400, [this, id](const QJsonObject& info, const QString& err) {
        if (!err.isEmpty() || info.isEmpty()) { emit message(err.isEmpty() ? tr("Album not found.") : err); return; }
        getAll("/album/" + id + "/tracks", 86400, 300, [this, info](const QJsonArray& tracks, const QString&) {
            QVector<Track> ts;
            for (const QJsonValue& v : tracks) ts.push_back(toTrack(v.toObject(), info));
            if (ts.isEmpty()) { emit message(tr("That album has no songs.")); return; }
            m_lib->importCatalog(ts);
            ++m_savedRev;
            emit savedChanged();
            emit message(tr("Added “%1” to your library").arg(info.value("title").toString()));
        });
    });
}

void Catalog::importTracks(const QStringList& paths)
{
    QVector<Track> ts;
    for (const QString& p : paths) if (isCatalogPath(p)) if (const auto t = m_lib->trackByPath(p)) ts.push_back(*t);
    if (ts.isEmpty()) return;
    m_lib->importCatalog(ts);
    ++m_savedRev;
    emit savedChanged();
    emit message(ts.size() == 1 ? tr("Added to your library") : tr("Added %1 songs to your library").arg(ts.size()));
}

void Catalog::removeTracks(const QStringList& paths)
{
    m_lib->removeCatalog(paths);
    ++m_savedRev;
    emit savedChanged();
}

void Catalog::removeArtist(const QString& id)
{
    QStringList gone;
    for (const Track& t : Db::get().catTracks(true)) if (t.artistId == id) gone << t.path;
    m_lib->removeCatalog(gone);
    Db::get().unsaveCatArtist(id);
    m_artistPages.remove(id);
    ++m_savedRev;
    emit savedChanged();
    emit message(tr("Removed from your library"));
}

void Catalog::openArtistByName(const QString& name)
{
    resolveArtist(name, [this, name](const std::optional<Db::CatArtist>& a) {
        if (a) emit navigateArtist(a->id);
        else emit message(tr("Couldn't find “%1” in the catalog.").arg(name));
    });
}
