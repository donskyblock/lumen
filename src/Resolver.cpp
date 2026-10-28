#include "Resolver.h"
#include "TextUtil.h"
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

namespace {
constexpr qint64 kFreshSec = 20 * 60;          // preview and stream addresses expire, so don't keep them long

// How well does a search hit from an extension match the song we want? <= 0 means "not it".
// `loose` is the second tier, used only after nothing passes the strict test: music-video uploads
// are often a minute longer than the studio track (intro, outro, talking), and that alone must not
// push a song down to the 30-second preview when a perfectly good full version exists.
double score(const Track& want, const QVariantMap& c, bool loose)
{
    const QString title = foldText(c.value("title").toString());
    const QString artist = foldText(c.value("artist").toString());
    const QString wantTitle = normTitleKey(want.title);
    // Same word-splitting as the candidate side below: "Mr.Kitty" and "AC/DC" must compare as "mr kitty" and "ac dc",
    // not keep their punctuation (which made every upload of such an artist score 0 and fall back to the preview).
    const QString wantArtist = wordsOf(primaryArtistKey(want.artist)).join(QLatin1Char(' '));
    if (wantTitle.isEmpty()) return 0;
    const QString candTitleKey = wordsOf(title).join(QLatin1Char(' '));
    if (!(candTitleKey.contains(wantTitle))) return 0;
    const QString candArtistKey = wordsOf(artist).join(QLatin1Char(' '));
    QString bare = candArtistKey;
    bare.remove(QStringLiteral(" topic"));
    bare = bare.trimmed();
    QString squashedWant = wantArtist, squashedCand = bare;       // "TheKillers" vs "The Killers"
    squashedWant.remove(' '); squashedCand.remove(' ');
    const bool artistOk = wantArtist.isEmpty() || candArtistKey.contains(wantArtist) || candTitleKey.contains(wantArtist)
        || (!bare.isEmpty() && wantArtist.contains(bare))
        || (!squashedCand.isEmpty() && (squashedCand.contains(squashedWant) || squashedWant.contains(squashedCand)));
    if (!artistOk) return 0;
    if (variantTag(c.value("title").toString()) != variantTag(want.title)) return 0;      // slowed, live, cover...
    double s = 1.0;
    if (normTitleKey(c.value("title").toString()) == wantTitle) s += 1.0;
    const qint64 d = c.value("durationMs").toLongLong();
    if (want.durationMs > 0 && d > 0) {
        const qint64 diff = d - want.durationMs;
        const qint64 tooShort = loose ? 20000 : 25000, tooLong = loose ? 75000 : 25000;
        if (diff < -tooShort || diff > tooLong) return 0;
        const qint64 ad = std::abs(diff);
        s += ad <= 6000 ? 1.0 : (ad <= 15000 ? 0.4 : 0);
    }
    if (artist.endsWith(QStringLiteral("- topic"))) s += 0.3;       // auto-generated "Artist - Topic" uploads are the studio audio
    if (title.contains(QStringLiteral("official audio"))) s += 0.2;
    if (title.contains(QStringLiteral("lyric")) || title.contains(QStringLiteral("visuali"))) s -= 0.1;
    return std::max(0.01, s);
}
} // namespace

Resolver::Resolver(Library* lib, PluginManager* plugins, Catalog* catalog, QObject* parent)
    : QObject(parent), m_lib(lib), m_plugins(plugins), m_catalog(catalog)
{
    connect(plugins, &PluginManager::changed, this, &Resolver::changed);
}

QStringList Resolver::fullSources() const
{
    QStringList names;
    for (const QString& id : m_plugins->enabledWith("source")) names << m_plugins->nameOf(id);
    return names;
}

std::optional<Resolved> Resolver::cached(const QString& path) const
{
    const auto it = m_cache.constFind(path);
    if (it == m_cache.constEnd()) return std::nullopt;
    if (QDateTime::currentSecsSinceEpoch() - it->at > kFreshSec) return std::nullopt;
    return *it;
}

void Resolver::resolve(const Track& t, Done cb)
{
    if (const auto hit = cached(t.path)) {
        const Resolved r = *hit;
        QTimer::singleShot(0, this, [cb, r] { cb(r); });
        return;
    }
    auto& waiters = m_waiting[t.path];
    waiters.push_back(std::move(cb));
    if (waiters.size() > 1) return;                 // already working on it

    if (const auto local = m_lib->matchLocal(t.title, t.artist, t.durationMs)) {
        Resolved r;
        r.ok = r.full = true;
        r.url = local->path;
        r.format = local->codec;
        r.lossless = local->lossless;
        r.bitrateKbps = local->bitrateKbps;
        r.sampleRate = local->sampleRate;
        r.bitDepth = local->bitDepth;
        r.durationMs = local->durationMs;
        r.via = tr("Your library");
        QTimer::singleShot(0, this, [this, p = t.path, r] { finish(p, r); });
        return;
    }
    if (isExtPath(t.path)) { resolveExt(t); return; }
    const QStringList ids = m_plugins->enabledWith("source");
    if (ids.isEmpty()) { preview(t); return; }
    auto c = std::make_shared<Ctx>();
    c->t = t;
    c->ids = ids;
    tryExtensions(c);
}

void Resolver::fallbackPreview(const Track& t, Done cb)
{
    if (!isCatalogPath(t.path)) {
        Resolved r;
        r.error = tr("This source could not be opened.");
        QTimer::singleShot(0, this, [cb, r] { cb(r); });
        return;
    }
    auto& waiters = m_waiting[t.path];
    waiters.push_back(std::move(cb));
    if (waiters.size() > 1) return;
    preview(t);
}

void Resolver::reportFailed(const QString& path, const Resolved& resolved)
{
    m_cache.remove(path);
    if (!resolved.providerId.isEmpty() && !resolved.providerItemId.isEmpty())
        m_failedCandidates.insert(path + QChar(0x1f) + resolved.providerId + QChar(0x1f) + resolved.providerItemId, QDateTime::currentSecsSinceEpoch());
}

// A result that failed to open is skipped for a while, not for the rest of the session:
// most failures (an expired link, a rate limit, a flaky connection) are gone ten minutes later.
bool Resolver::banned(const QString& path, const QString& pluginId, const QString& itemId) const
{
    const auto it = m_failedCandidates.constFind(path + QChar(0x1f) + pluginId + QChar(0x1f) + itemId);
    return it != m_failedCandidates.constEnd() && QDateTime::currentSecsSinceEpoch() - *it < 600;
}

void Resolver::finish(const QString& path, Resolved r)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    r.at = now;
    if (r.ok) {
        Resolved kept = r;
        // A 30-second preview that was only used because no full source answered (flaky network,
        // yt-dlp hiccup) must not stick for twenty minutes: let it expire after 45 s so the next
        // play of that song tries the full sources again.
        if (!r.full && !m_plugins->enabledWith("source").isEmpty()) kept.at = now - kFreshSec + 45;
        m_cache.insert(path, kept);
    }
    const QVector<Done> waiters = m_waiting.take(path);
    for (const Done& d : waiters) d(r);
}

void Resolver::tryExtensions(std::shared_ptr<Ctx> c)
{
    if (c->idx >= c->ids.size()) {
        if (c->upgrade) { Resolved r; r.error = c->lastError; c->upgrade(r); return; }
        fallback(c->t);
        return;
    }
    const QString id = c->ids[c->idx];
    const Track& t = c->t;
    // Pass 0 asks for the studio recording; pass 1 uses the plain "Artist - Title" wording that finds
    // the lyric-video / topic uploads the first query can miss. Together they decide the result far
    // more reliably than one query that has to be perfect.
    const QString q = c->pass == 0 ? t.artist + QLatin1Char(' ') + t.title + QStringLiteral(" official audio")
                                   : t.artist + QStringLiteral(" - ") + t.title;
    auto advance = [this, c] {
        if (++c->pass < 2) { tryExtensions(c); return; }
        c->pass = 0;
        ++c->idx;
        tryExtensions(c);
    };
    m_plugins->call(id, "source.search", {{"query", q}, {"limit", 16}}, [this, c, id, advance](bool ok, const QJsonValue& v, const QString& searchErr) {
        if (!ok && !searchErr.isEmpty()) c->lastError = searchErr;
        QVector<QPair<double, QVariantMap>> strict, loose;
        QSet<QString> seen;                      // the same upload can appear under two searches
        if (ok) for (const QJsonValue& x : v.toArray()) {
            QVariantMap m = x.toObject().toVariantMap();
            if (m.value("id").toString().isEmpty() || c->tried.contains(m.value("id").toString()) || seen.contains(m.value("id").toString())) continue;
            seen.insert(m.value("id").toString());
            if (const double s = score(c->t, m, false); s > 0) strict.push_back({s, m});
            else if (const double l = score(c->t, m, true); l > 0) loose.push_back({l, m});
        }
        auto byScore = [](const auto& a, const auto& b) { return a.first > b.first; };
        std::stable_sort(strict.begin(), strict.end(), byScore);
        std::stable_sort(loose.begin(), loose.end(), byScore);
        QVector<QVariantMap> cands;
        for (const auto& p : std::as_const(strict)) if (cands.size() < 6) cands.push_back(p.second);
        for (const auto& p : std::as_const(loose)) if (cands.size() < 6) cands.push_back(p.second);
        tryCandidates(c, id, cands, 0, advance);
    }, 25000);
}

void Resolver::tryCandidates(std::shared_ptr<Ctx> c, const QString& pluginId, QVector<QVariantMap> cands, int ci, std::function<void()> next)
{
    if (ci >= cands.size()) { next(); return; }
    const QVariantMap cand = cands[ci];
    const QString itemId = cand.value("id").toString();
    c->tried.insert(itemId);
    if (banned(c->t.path, pluginId, itemId)) { tryCandidates(c, pluginId, cands, ci + 1, next); return; }
    m_plugins->call(pluginId, "source.resolve", {{"id", itemId}},
        [this, c, pluginId, itemId, cands, ci, next, cand](bool ok, const QJsonValue& v, const QString& resolveErr) {
            const QJsonObject o = v.toObject();
            const QString url = o.value("url").toString();
            if (!ok || url.isEmpty()) { if (!resolveErr.isEmpty()) c->lastError = resolveErr; tryCandidates(c, pluginId, cands, ci + 1, next); return; }
            Resolved r;
            r.ok = r.full = true;
            r.url = url;
            const QJsonObject hdr = o.value("headers").toObject();
            for (auto it = hdr.begin(); it != hdr.end(); ++it)
                r.headers += it.key() + QStringLiteral(": ") + it.value().toString() + QStringLiteral("\r\n");
            r.format = o.value("format").toString().toUpper();
            r.lossless = o.value("lossless").toBool();
            r.sampleRate = o.value("sampleRate").toInt();
            r.bitDepth = o.value("bitDepth").toInt();
            r.bitrateKbps = o.value("bitrateKbps").toInt();
            r.durationMs = cand.value("durationMs").toLongLong();
            if (r.durationMs <= 0) r.durationMs = c->t.durationMs;
            r.via = m_plugins->nameOf(pluginId);
            r.providerId = pluginId;
            r.providerItemId = itemId;
            deliver(c, r);
        }, 45000);
}

void Resolver::deliver(const std::shared_ptr<Ctx>& c, Resolved r)
{
    if (!c->upgrade) { finish(c->t.path, r); return; }
    r.at = QDateTime::currentSecsSinceEpoch();
    m_cache.insert(c->t.path, r);                  // replaces the short-lived preview entry: replays start in full
    c->upgrade(r);
}

void Resolver::upgrade(const Track& t, Done cb)
{
    const QStringList ids = m_plugins->enabledWith("source");
    if (ids.isEmpty()) { Resolved r; r.error = tr("No source extension is switched on."); QTimer::singleShot(0, this, [cb, r] { cb(r); }); return; }
    auto c = std::make_shared<Ctx>();
    c->t = t;
    c->ids = ids;
    c->upgrade = std::move(cb);
    tryExtensions(c);
}

void Resolver::fallback(const Track& t)
{
    if (isCatalogPath(t.path)) { preview(t); return; }
    Resolved r;
    r.error = tr("No playable source was found for this song.");
    finish(t.path, r);
}

void Resolver::preview(const Track& t)
{
    m_catalog->freshPreview(catalogId(t.path), [this, p = t.path](const QString& url, qint64, const QString& err) {
        Resolved r;
        if (url.isEmpty()) {
            r.error = err.isEmpty() ? tr("No playable source for this song.") : err;
            finish(p, r);                          // not cached (ok == false)
            return;
        }
        r.ok = true;
        r.full = false;
        r.url = url;
        r.format = QStringLiteral("MP3");
        r.bitrateKbps = 128;
        r.durationMs = 30000;
        r.via = tr("30-second preview");
        finish(p, r);
    });
}

// A song that an extension itself recommended or listed: ask that extension for the stream. If that
// one result no longer opens, look for another upload of the same song instead of giving up.
void Resolver::resolveExt(const Track& t, int attempt)
{
    const QString rest = t.path.mid(int(QStringLiteral("lumen://ext/").size()));
    const int slash = rest.indexOf(QLatin1Char('/'));
    const QString pluginId = rest.left(slash), itemId = rest.mid(slash + 1);
    auto alternatives = [this, t, pluginId, itemId] {
        if (!m_plugins->enabledWith("source").contains(pluginId)) { fallback(t); return; }
        auto c = std::make_shared<Ctx>();
        c->t = t;
        c->ids = {pluginId};
        c->tried.insert(itemId);
        tryExtensions(c);
    };
    if (banned(t.path, pluginId, itemId)) { alternatives(); return; }
    m_plugins->call(pluginId, "source.resolve", {{"id", itemId}}, [this, t, pluginId, itemId, attempt, alternatives](bool ok, const QJsonValue& v, const QString&) {
        const QJsonObject o = v.toObject();
        const QString url = o.value("url").toString();
        if (!ok || url.isEmpty()) {
            if (attempt == 0) { resolveExt(t, 1); return; }       // one more go: most failures are momentary
            alternatives();
            return;
        }
        Resolved r;
        r.ok = r.full = true;
        r.url = url;
        const QJsonObject hdr = o.value("headers").toObject();
        for (auto it = hdr.begin(); it != hdr.end(); ++it)
            r.headers += it.key() + QStringLiteral(": ") + it.value().toString() + QStringLiteral("\r\n");
        r.format = o.value("format").toString().toUpper();
        r.lossless = o.value("lossless").toBool();
        r.sampleRate = o.value("sampleRate").toInt();
        r.bitDepth = o.value("bitDepth").toInt();
        r.bitrateKbps = o.value("bitrateKbps").toInt();
        r.durationMs = t.durationMs;
        r.via = m_plugins->nameOf(pluginId);
        r.providerId = pluginId;
        r.providerItemId = itemId;
        finish(t.path, r);
    }, 45000);
}
