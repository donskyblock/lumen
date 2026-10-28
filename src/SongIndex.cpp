#include "SongIndex.h"
#include "Db.h"
#include "PlayStats.h"
#include "TextUtil.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 kMaxSongs = 100000;                 // measured: about 24 MB for 100,000 songs with their artists and links
constexpr qint64 kRecrawlSec = 21LL * 86400;         // an artist is re-read at most every three weeks
constexpr int kDailyArtistCap = 300;                 // be polite to the catalog: at most this many artists a day
constexpr qint64 kSessionGapSec = 40 * 60;           // plays further apart than this are not "one after the other"
}

qint64 SongIndex::hashOf(const QString& title, const QString& artist)
{
    const QString key = normTitleKey(title) + QChar(0x1f) + primaryArtistKey(artist) + QChar(0x1f) + variantTag(title);
    const QByteArray d = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5);
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | quint8(d[i]);
    return qint64(v & 0x7FFFFFFFFFFFFFFFULL);
}

SongIndex::SongIndex(Catalog* catalog, Library* library, QObject* parent) : QObject(parent), m_catalog(catalog), m_library(library)
{
    m_passive = Db::get().kv("index.passive", "1") == "1";
    m_crawl.setSingleShot(true);
    connect(&m_crawl, &QTimer::timeout, this, &SongIndex::crawlTick);
    m_queryTimer.setSingleShot(true);
    m_queryTimer.setInterval(2500);
    connect(&m_queryTimer, &QTimer::timeout, this, [this] {
        const QString q = m_pendingQuery.simplified();
        m_pendingQuery.clear();
        if (q.size() < 3) return;
        // does the query name an artist? (strict match, so "lucid dreams" never counts as an artist)
        m_catalog->resolveArtist(q, [this, q](const std::optional<Db::CatArtist>& a) {
            QString key;
            if (a) {
                key = primaryArtistKey(a->name);
                Db::get().idxTouchArtist(key, a->name, a->id, 0.7);
            }
            Db::get().idxNoteSearch(q.toLower(), key, QDateTime::currentSecsSinceEpoch());
            emit changed();
        });
    });
}

void SongIndex::start()
{
    backfill();
    Db::get().idxPrune(kMaxSongs);
    if (m_passive) m_crawl.start(25000);              // first look 25 s after start-up, not during it
}

void SongIndex::setPassive(bool on)
{
    if (m_passive == on) return;
    m_passive = on;
    Db::get().setKv("index.passive", on ? "1" : "0");
    if (on) m_crawl.start(10000); else m_crawl.stop();
    emit changed();
}

QVariantMap SongIndex::stats() const
{
    const Db::IdxCounts c = Db::get().idxCounts();
    return {{"songs", c.songs}, {"artists", c.artists}, {"links", c.edges}, {"searches", c.searches}, {"crawled", c.crawled}, {"passive", m_passive}};
}

// ---- learning --------------------------------------------------------------------------------------------

static Db::IdxSong toRow(const Track& t, int flags)
{
    Db::IdxSong x;
    x.h = SongIndex::hashOf(t);
    x.path = t.path; x.title = t.title; x.artist = t.artist; x.key = primaryArtistKey(t.artist);
    x.album = t.album; x.artistId = t.artistId; x.cover = t.coverUrl; x.durationMs = t.durationMs; x.rank = t.rank; x.flags = flags;
    return x;
}

void SongIndex::noteSongs(const QVector<Track>& tracks, int flags)
{
    QVector<Db::IdxSong> rows;
    for (const Track& t : tracks) {
        if (!needsResolve(t.path) || t.title.isEmpty() || t.artist.isEmpty()) continue;      // your own files are already your library
        rows.push_back(toRow(t, flags));
    }
    if (rows.isEmpty()) return;
    Db::get().idxUpsertSongs(rows);
    for (const Db::IdxSong& r : std::as_const(rows)) {
        // a first sighting makes the artist known (weight 0: it only becomes a crawl target once you care about it, or via a neighbour)
        Db::get().idxTouchArtist(r.key, r.artist, r.artistId, 0.0);
    }
}

void SongIndex::noteListen(const Track& t)
{
    if (t.title.isEmpty() || t.artist.isEmpty()) return;
    const qint64 h = hashOf(t);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString key = primaryArtistKey(t.artist);
    if (needsResolve(t.path)) Db::get().idxUpsertSongs({toRow(t, 1)});
    Db::get().idxTouchArtist(key, t.artist, t.artistId, 1.0);
    // "this came right after that": the strongest hint of what goes together
    if (m_prevHash && m_prevHash != h && now - m_prevAt < kSessionGapSec) {
        Db::get().idxAddPair(m_prevHash, h, 1.0);
        Db::get().idxAddPair(h, m_prevHash, 0.4);
        if (!m_prevArtist.isEmpty() && m_prevArtist != key) Db::get().idxAddEdge(m_prevArtist, key, 0.4);
    }
    m_prevHash = h; m_prevAt = now; m_prevArtist = key;
    emit changed();
}

void SongIndex::noteQuery(const QString& query)
{
    m_pendingQuery = query;
    if (query.simplified().size() >= 3) m_queryTimer.start(); else m_queryTimer.stop();
}

// First run (and after upgrading): turn the existing listening history into artist weights, so the
// passive indexer knows whose neighbourhood to explore from day one.
void SongIndex::backfill()
{
    if (Db::get().kv("index.backfilled", "") == "1") return;
    QHash<QString, QPair<QString, double>> weights;                 // artist key -> (name, plays)
    for (const auto& ev : m_library->stats()->events()) {
        const auto t = m_library->trackByPath(ev.path);
        if (!t || t->artist.isEmpty()) continue;
        auto& w = weights[primaryArtistKey(t->artist)];
        w.first = t->artist;
        w.second += 1.0;
    }
    for (auto it = weights.begin(); it != weights.end(); ++it)
        Db::get().idxTouchArtist(it.key(), it->first, QString(), std::min(12.0, it->second));
    Db::get().setKv("index.backfilled", "1");
}

// ---- autoplay candidates ------------------------------------------------------------------------------------

Track SongIndex::toTrack(const Db::IdxSong& s) const
{
    Track t;
    t.path = s.path; t.title = s.title; t.artist = s.artist; t.album = s.album; t.artistId = s.artistId;
    t.coverUrl = s.cover; t.durationMs = s.durationMs; t.rank = s.rank;
    t.codec = isExtPath(s.path) ? QStringLiteral("Stream") : QString();
    return t;
}

QVector<SongIndex::Pick> SongIndex::candidates(const QVector<Track>& seeds, int n)
{
    QHash<qint64, Pick> out;                                          // best evidence per recording
    QSet<qint64> seedHashes;
    for (const Track& t : seeds) seedHashes.insert(hashOf(t));
    auto offer = [&](const Db::IdxSong& s, double w, const QString& why) {
        if (s.path.isEmpty() || s.title.isEmpty() || seedHashes.contains(s.h)) return;      // never the song that is playing
        auto it = out.find(s.h);
        if (it == out.end()) out.insert(s.h, {toTrack(s), w, why});
        else if (w > it->w) { it->w = w; it->why = why; }
    };
    auto songsOf = [&](const QString& key, double w, int cap, const QString& why) {
        for (const Db::IdxSong& s : Db::get().idxSongsByArtist(key, cap)) offer(s, w * (s.flags & 1 ? 1.15 : 1.0), why);
    };

    QSet<QString> seedArtists;
    for (int i = std::max(0, int(seeds.size()) - 2); i < seeds.size(); ++i) {
        const Track& seed = seeds[i];
        const double recency = i == seeds.size() - 1 ? 1.0 : 0.6;
        const qint64 sh = hashOf(seed);
        const QString key = primaryArtistKey(seed.artist);
        seedArtists.insert(key);
        // 1. what you have played right after this song before
        QVector<qint64> hs;
        QHash<qint64, double> pw;
        for (const auto& p : Db::get().idxPairsFrom(sh, 12)) { hs << p.first; pw[p.first] = p.second; }
        for (const Db::IdxSong& s : Db::get().idxSongsByHash(hs)) offer(s, recency * (1.2 + std::min(1.0, pw.value(s.h) / 3.0)), QStringLiteral("played after"));
        // 2. the artist's own songs we know, and the artists that sit next to them (catalog "related" + co-listening)
        songsOf(key, recency * 0.85, 8, QStringLiteral("same artist"));
        int rank = 0;
        for (const auto& r : Db::get().idxRelated(key, 8)) {
            songsOf(r.first, recency * (0.5 + 0.4 * std::min(1.0, r.second / 3.0)) / (1.0 + 0.15 * rank++), 5, QStringLiteral("related artist"));
        }
    }
    // 3. the wider picture: artists you keep coming back to and artists behind your recent searches,
    //    plus their neighbours. A smaller share, so the song playing now still steers the queue.
    int shown = 0;
    for (const Db::IdxArtist& a : Db::get().idxTopArtists(10)) {
        if (seedArtists.contains(a.key)) continue;
        const double w = 0.45 * std::min(1.0, std::log10(1.0 + a.w) / 1.3);
        songsOf(a.key, w, 4, QStringLiteral("you listen to"));
        if (++shown <= 4) for (const auto& r : Db::get().idxRelated(a.key, 3)) songsOf(r.first, w * 0.7, 3, QStringLiteral("near what you play"));
    }
    for (const auto& s : Db::get().idxRecentSearches(8)) {
        if (s.second.isEmpty() || seedArtists.contains(s.second)) continue;
        songsOf(s.second, 0.5, 4, QStringLiteral("you searched"));
        for (const auto& r : Db::get().idxRelated(s.second, 3)) songsOf(r.first, 0.35, 3, QStringLiteral("near what you searched"));
    }

    QVector<Pick> picks = out.values().toVector();
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.w > b.w; });
    if (picks.size() > n) picks.resize(n);
    return picks;
}

// ---- passive indexing --------------------------------------------------------------------------------------

void SongIndex::scheduleNext(bool ok)
{
    m_failStreak = ok ? 0 : std::min(m_failStreak + 1, 5);
    // ~40 s between artists normally; back off up to ~20 min while the catalog is unreachable
    if (m_passive) m_crawl.start(40000 * (1 << m_failStreak));
}

void SongIndex::crawlTick()
{
    if (!m_passive || m_busy) return;
    if (!m_catalog->online()) { scheduleNext(true); return; }
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    int doneToday = Db::get().kv("index.day", "") == today ? Db::get().kv("index.dayCount", "0").toInt() : 0;
    if (doneToday >= kDailyArtistCap) { m_crawl.start(30 * 60 * 1000); return; }
    const auto todo = Db::get().idxArtistsToCrawl(1, QDateTime::currentSecsSinceEpoch() - kRecrawlSec);
    if (todo.isEmpty()) { m_crawl.start(10 * 60 * 1000); return; }
    Db::get().setKv("index.day", today);
    Db::get().setKv("index.dayCount", QString::number(doneToday + 1));
    crawlArtist(todo.first());
}

void SongIndex::crawlArtist(const Db::IdxArtist& a)
{
    m_busy = true;
    auto finish = [this, key = a.key](bool ok) {
        // an artist that could not be read is retried much later, not in a loop
        Db::get().idxMarkCrawled(key, QDateTime::currentSecsSinceEpoch() - (ok ? 0 : kRecrawlSec - 86400));
        m_busy = false;
        emit changed();
        scheduleNext(ok);
    };
    auto readCatalog = [this, a, finish](const QString& cid) {
        m_catalog->topTracks(cid, [this, a, cid, finish](const QVector<Track>& tracks) {
            QVector<Track> keep;
            for (const Track& t : tracks) keep.push_back(t);
            noteSongs(keep, 2);
            m_catalog->relatedArtists(cid, [this, a, finish, got = !tracks.isEmpty()](const QVector<Db::CatArtist>& rel) {
                int i = 0;
                for (const Db::CatArtist& r : rel) {
                    if (i >= 10) break;
                    const QString rk = primaryArtistKey(r.name);
                    // neighbours inherit a little of this artist's importance, so exploration fans out from what you like
                    Db::get().idxTouchArtist(rk, r.name, r.id, std::max(0.05, 0.25 * std::min(a.w, 6.0) / (1.0 + 0.2 * i)));
                    Db::get().idxAddEdge(a.key, rk, 1.0 / (1.0 + 0.25 * i));
                    ++i;
                }
                finish(got || !rel.isEmpty());
            });
        });
    };
    if (!a.cid.isEmpty()) { readCatalog(a.cid); return; }
    m_catalog->resolveArtist(a.name, [this, a, finish, readCatalog](const std::optional<Db::CatArtist>& found) {
        if (!found) { finish(false); return; }
        Db::get().idxTouchArtist(a.key, a.name, found->id, 0.0);
        readCatalog(found->id);
    });
}
