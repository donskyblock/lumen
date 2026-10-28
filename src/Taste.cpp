#include "Taste.h"
#include "SongIndex.h"
#include "TextUtil.h"
#include <QDate>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
struct ParamDef { const char* key; const char* label; const char* help; double def, min, max; };
const ParamDef kParams[] = {
    {"recency",     "Memory",          "How many days until a play counts half as much. Lower = follows your mood, higher = long-term taste.", 45, 7, 365},
    {"exploration", "Exploration",     "Share of each mix that comes from artists you haven't played yet.",                          0.35, 0, 1},
    {"novelty",     "Novelty",         "Favour songs you haven't heard over ones you've worn out.",                                  0.5, 0, 1},
    {"popularity",  "Popularity",      "Lean toward an artist's best-known songs instead of deep cuts.",                              0.5, 0, 1},
    {"likes",       "Likes weight",    "How strongly a like counts compared with a single play.",                                     1.5, 0, 3},
    {"skips",       "Skips weight",    "How strongly skipping a song early counts against it and its artist.",                        1.0, 0, 3},
    {"diversity",   "Variety",         "How far apart songs by the same artist are placed.",                                          0.7, 0, 1},
};

QString localPath(const QString& s)
{
    if (s.startsWith(QLatin1String("file:"))) return QUrl(s).toLocalFile();
    if (s == QLatin1String("~") || s.startsWith(QLatin1String("~/"))) return QDir::homePath() + s.mid(1);
    return s;
}

QVector<int> weightedPick(const QVector<double>& w, int n, QRandomGenerator& rng)
{
    QVector<QPair<double, int>> keyed;
    keyed.reserve(w.size());
    for (int i = 0; i < w.size(); ++i) {
        double u = rng.generateDouble();
        if (u < 1e-12) u = 1e-12;
        keyed.push_back({std::pow(u, 1.0 / std::max(1e-6, w[i])), i});
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QVector<int> out;
    for (int i = 0; i < keyed.size() && i < n; ++i) out.push_back(keyed[i].second);
    return out;
}
} // namespace

Taste::Taste(Library* lib, Catalog* catalog, Playlists* playlists, QObject* parent)
    : QObject(parent), m_lib(lib), m_catalog(catalog), m_playlists(playlists)
{
    loadParams();
    m_lib->stats()->setTauDays(m_params.value("recency"));
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(600);
    connect(&m_refresh, &QTimer::timeout, this, &Taste::refreshProfile);
    connect(m_lib->stats(), &PlayStats::changed, &m_refresh, qOverload<>(&QTimer::start));
    connect(m_playlists, &Playlists::changed, &m_refresh, qOverload<>(&QTimer::start));
    connect(m_catalog, &Catalog::savedChanged, &m_refresh, qOverload<>(&QTimer::start));
    connect(m_lib, &Library::changed, &m_refresh, qOverload<>(&QTimer::start));
    refreshProfile();
}

// ---- knobs -------------------------------------------------------------------------------

void Taste::loadParams()
{
    for (const ParamDef& d : kParams) m_params[d.key] = d.def;
    const QJsonObject o = QJsonDocument::fromJson(Db::get().kv("taste.params").toUtf8()).object();
    for (const ParamDef& d : kParams)
        if (o.contains(d.key)) m_params[d.key] = std::clamp(o.value(d.key).toDouble(d.def), d.min, d.max);
}

void Taste::saveParams() const
{
    QJsonObject o;
    for (const ParamDef& d : kParams) o[d.key] = m_params.value(d.key);
    Db::get().setKv("taste.params", QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

QVariantList Taste::paramInfo() const
{
    QVariantList out;
    for (const ParamDef& d : kParams)
        out << QVariantMap{{"key", d.key}, {"label", d.label}, {"help", d.help}, {"min", d.min}, {"max", d.max}, {"def", d.def},
                           {"value", m_params.value(d.key)}};
    return out;
}

void Taste::setParam(const QString& key, double value)
{
    for (const ParamDef& d : kParams) {
        if (key != QLatin1String(d.key)) continue;
        const double v = std::clamp(value, d.min, d.max);
        if (qFuzzyCompare(v + 1, m_params.value(key) + 1)) return;
        m_params[key] = v;
        saveParams();
        if (key == QLatin1String("recency")) m_lib->stats()->setTauDays(v);
        emit paramsChanged();
        m_refresh.start();
        return;
    }
}

void Taste::resetParams()
{
    for (const ParamDef& d : kParams) m_params[d.key] = d.def;
    saveParams();
    m_lib->stats()->setTauDays(m_params.value("recency"));
    emit paramsChanged();
    m_refresh.start();
}

void Taste::setBusy(const QString& s) { if (s != m_busy) { m_busy = s; emit busyChanged(); } }

// ---- the profile ---------------------------------------------------------------------------

QHash<QString, Taste::Affinity> Taste::computeProfile() const
{
    QHash<QString, Affinity> aff;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const double tau = m_params.value("recency") * 86400.0;
    auto bump = [&](const QString& artistField, double amount) {
        const QStringList names = splitArtists(artistField);
        for (int i = 0; i < names.size(); ++i) {
            const QString key = primaryArtistKey(names[i]);
            if (key.isEmpty()) continue;
            Affinity& a = aff[key];
            if (a.name.isEmpty()) a.name = names[i];
            a.score += amount * (i == 0 ? 1.0 : 0.5);          // featured artists count half
        }
    };
    for (const auto& e : m_lib->stats()->events())
        if (const auto t = m_lib->trackByPath(e.path)) bump(t->artist, std::exp(-double(now - e.at) / tau));
    for (const QString& p : m_playlists->likedPaths())
        if (const auto t = m_lib->trackByPath(p)) bump(t->artist, m_params.value("likes"));
    for (const Db::CatArtist& a : Db::get().savedCatArtists()) bump(a.name, 2.0);
    const double skipW = m_params.value("skips");
    if (skipW > 0)
        for (const auto& s : Db::get().skips(now - 180LL * 86400))
            if (const auto t = m_lib->trackByPath(s.path)) bump(t->artist, -0.5 * skipW * std::exp(-double(now - s.at) / tau));
    double mx = 0;
    for (auto it = aff.begin(); it != aff.end(); ++it) { it->score = std::max(0.0, it->score); mx = std::max(mx, it->score); }
    if (mx > 0) for (auto it = aff.begin(); it != aff.end(); ++it) it->score /= mx;
    return aff;
}

void Taste::refreshProfile()
{
    m_profile = computeProfile();
    QVector<Affinity> v;
    for (const Affinity& a : std::as_const(m_profile)) if (a.score > 0.001) v.push_back(a);
    std::sort(v.begin(), v.end(), [](const Affinity& a, const Affinity& b) { return a.score > b.score; });
    m_profileList.clear();
    for (int i = 0; i < v.size() && i < 12; ++i) m_profileList << QVariantMap{{"name", v[i].name}, {"affinity", v[i].score}};
    m_signals = int(m_lib->stats()->events().size()) + int(m_playlists->likedPaths().size());
    emit profileChanged();
}

QVector<QPair<QString, double>> Taste::seedArtists(int n) const
{
    QVector<Affinity> v;
    for (const Affinity& a : m_profile) if (a.score > 0.02) v.push_back(a);
    std::sort(v.begin(), v.end(), [](const Affinity& a, const Affinity& b) { return a.score > b.score; });
    QVector<QPair<QString, double>> out;
    for (int i = 0; i < v.size() && i < n; ++i) out.push_back({v[i].name, v[i].score});
    return out;
}

void Taste::resolveSeeds(const QVector<QPair<QString, double>>& names, std::function<void(const QVector<QPair<Db::CatArtist, double>>&)> cb)
{
    struct State { QVector<QPair<Db::CatArtist, double>> found; int left; };
    auto st = std::make_shared<State>();
    st->left = int(names.size());
    if (names.isEmpty()) { cb({}); return; }
    for (const auto& n : names) {
        m_catalog->resolveArtist(n.first, [st, w = n.second, cb](const std::optional<Db::CatArtist>& a) {
            if (a) st->found.push_back({*a, w});
            if (--st->left == 0) {
                std::sort(st->found.begin(), st->found.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
                cb(st->found);
            }
        });
    }
}

// ---- discover ------------------------------------------------------------------------------

void Taste::loadDiscover()
{
    const auto seeds = seedArtists(5);
    if (seeds.isEmpty()) { m_discover.clear(); m_discoverReason.clear(); emit discoverChanged(); return; }
    const int job = ++m_job;
    resolveSeeds(seeds, [this, job](const QVector<QPair<Db::CatArtist, double>>& resolved) {
        if (job != m_job || resolved.isEmpty()) return;
        struct State { QHash<QString, QPair<Db::CatArtist, double>> score; QHash<QString, QString> because; int left; };
        auto st = std::make_shared<State>();
        st->left = int(resolved.size());
        for (const auto& seed : resolved) {
            m_catalog->relatedArtists(seed.first.id, [this, st, seed, job](const QVector<Db::CatArtist>& rel) {
                for (int i = 0; i < rel.size(); ++i) {
                    const Db::CatArtist& a = rel[i];
                    auto& slot = st->score[a.id];
                    slot.first = a;
                    const double s = seed.second / (1.0 + 0.35 * i);
                    if (s > 0 && (!st->because.contains(a.id) || s > slot.second * 0.5)) st->because[a.id] = seed.first.name;
                    slot.second += s;
                }
                if (--st->left > 0 || job != m_job) return;
                QVector<QPair<Db::CatArtist, double>> all;
                for (const auto& p : std::as_const(st->score)) {
                    const auto it = m_profile.constFind(primaryArtistKey(p.first.name));
                    if (it != m_profile.constEnd() && it->score > 0.05) continue;      // you already know them
                    if (p.first.saved) continue;
                    all.push_back(p);
                }
                std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                m_discover.clear();
                QHash<QString, int> byReason;
                for (int i = 0; i < all.size() && m_discover.size() < 14; ++i) {
                    const auto& a = all[i].first;
                    m_discover << QVariantMap{{"id", a.id}, {"name", a.name}, {"picture", a.picture}, {"because", st->because.value(a.id)}};
                    byReason[st->because.value(a.id)]++;
                }
                QString best;
                int bc = 0;
                for (auto it = byReason.cbegin(); it != byReason.cend(); ++it) if (it.value() > bc) { bc = it.value(); best = it.key(); }
                m_discoverReason = best.isEmpty() ? QString() : tr("Because you listen to %1").arg(best);
                emit discoverChanged();
            });
        }
    });
}

// ---- radio -----------------------------------------------------------------------------------

void Taste::startRadio(const QString& seed)
{
    if (!m_busy.isEmpty() && m_busy.startsWith(tr("Building"))) return;
    if (seed.startsWith(QLatin1String("artist:"))) {
        const QString id = seed.mid(7);
        setBusy(tr("Building radio…"));
        const auto known = Db::get().catArtist(id);
        const Db::CatArtist a = known ? *known : Db::CatArtist{id, tr("Artist"), {}, 0, false};
        buildRadio(tr("%1 Radio").arg(a.name), {{a, 1.0}}, true);
        return;
    }
    setBusy(tr("Building your radio…"));
    auto seeds = seedArtists(4);
    if (seeds.isEmpty()) {                          // no history yet: start from saved artists, then from the charts
        for (const Db::CatArtist& a : Db::get().savedCatArtists()) if (seeds.size() < 4) seeds.push_back({a.name, 1.0});
    }
    if (seeds.isEmpty()) {
        QVector<QPair<Db::CatArtist, double>> fromCharts;
        for (const QVariant& v : m_catalog->charts().value("artists").toList()) {
            const QVariantMap m = v.toMap();
            if (fromCharts.size() < 4) fromCharts.push_back({Db::CatArtist{m.value("id").toString(), m.value("name").toString(), m.value("picture").toString(), 0, false}, 1.0});
        }
        if (fromCharts.isEmpty()) { setBusy({}); emit radioFailed(tr("Play or search for a few artists first and your radio will have something to learn from.")); return; }
        buildRadio(tr("Lumen Radio"), fromCharts, false);
        return;
    }
    resolveSeeds(seeds, [this](const QVector<QPair<Db::CatArtist, double>>& r) {
        if (r.isEmpty()) { setBusy({}); emit radioFailed(tr("Couldn't reach the catalog to build your radio.")); return; }
        buildRadio(tr("Your Radio"), r, false);
    });
}

void Taste::buildRadio(const QString& title, const QVector<QPair<Db::CatArtist, double>>& seeds, bool artistRadio)
{
    struct Cand { Track t; double base; bool familiar; };
    struct State {
        QVector<Cand> cands;
        int left = 0;
        bool started = false;
    };
    auto st = std::make_shared<State>();
    const int job = ++m_job;

    auto finish = [this, st, title, artistRadio, job] {
        if (job != m_job) return;
        setBusy({});
        if (st->cands.isEmpty()) { emit radioFailed(tr("Couldn't reach the catalog to build your radio.")); return; }
        const double popularity = m_params.value("popularity"), novelty = m_params.value("novelty"), likesW = m_params.value("likes");
        const double skipsW = m_params.value("skips"), exploration = m_params.value("exploration"), diversity = m_params.value("diversity");
        const QStringList likedList = m_playlists->likedPaths();
        const QSet<QString> liked(likedList.begin(), likedList.end());
        QHash<QString, int> skipCount;
        for (const auto& s : Db::get().skips(QDateTime::currentSecsSinceEpoch() - 180LL * 86400)) skipCount[s.path]++;

        // drop duplicates of the same recording, keep the most popular version
        QHash<QString, int> bestByKey;
        for (int i = 0; i < st->cands.size(); ++i) {
            const Track& t = st->cands[i].t;
            const QString k = normTitleKey(t.title) + QChar(0x1f) + variantTag(t.title) + QChar(0x1f) + primaryArtistKey(t.artist);
            const auto it = bestByKey.constFind(k);
            if (it == bestByKey.constEnd() || st->cands[*it].t.rank < t.rank) bestByKey[k] = i;
        }
        QVector<Cand> pool;
        for (auto it = bestByKey.cbegin(); it != bestByKey.cend(); ++it) pool.push_back(st->cands[it.value()]);

        QVector<double> wFam, wNew;
        QVector<int> iFam, iNew;
        for (int i = 0; i < pool.size(); ++i) {
            const Cand& c = pool[i];
            const double pop = 1.0 + popularity * 2.0 * std::min(1.0, std::log10(1.0 + c.t.rank) / 6.0);
            const int plays = m_lib->stats()->count(c.t.path);
            const double nov = plays == 0 ? 1.0 + novelty : 1.0 / (1.0 + novelty * 0.4 * plays);
            double w = c.base * pop * nov;
            if (liked.contains(c.t.path)) w *= 1.0 + 0.3 * likesW;
            w /= 1.0 + skipsW * skipCount.value(c.t.path);
            w = std::max(0.001, w);
            if (c.familiar) { wFam << w; iFam << i; } else { wNew << w; iNew << i; }
        }
        QRandomGenerator rng(quint32(QDateTime::currentMSecsSinceEpoch()));
        const int total = 40;
        int nNew = iNew.isEmpty() ? 0 : int(std::round(total * (artistRadio ? std::max(exploration, 0.3) : exploration)));
        int nFam = total - nNew;
        if (iFam.isEmpty()) { nNew = total; nFam = 0; }
        QVector<int> chosen;                                      // indices into pool, in sampled order
        for (int k : weightedPick(wFam, nFam, rng)) chosen << iFam[k];
        for (int k : weightedPick(wNew, nNew, rng)) chosen << iNew[k];
        if (chosen.size() < total) {                              // top up from whichever pool still has songs
            QSet<int> have(chosen.begin(), chosen.end());
            for (int i = 0; i < pool.size() && chosen.size() < total; ++i) if (!have.contains(i)) chosen << i;
        }
        // order: spread an artist's songs apart; the first song is a well-known one from the closest seed
        const int gap = std::max(1, int(std::round(diversity * 4)));
        QVector<int> ordered;
        QVector<QString> recent;
        QVector<int> rest = chosen;
        std::shuffle(rest.begin(), rest.end(), *QRandomGenerator::global());
        if (artistRadio || !iFam.isEmpty()) {
            int firstAt = -1;
            double best = -1;
            for (int i = 0; i < rest.size(); ++i) if (pool[rest[i]].familiar && pool[rest[i]].t.rank > best) { best = pool[rest[i]].t.rank; firstAt = i; }
            if (firstAt >= 0) { rest.move(firstAt, 0); }
        }
        while (!rest.isEmpty()) {
            int pick = 0;
            for (int i = 0; i < rest.size(); ++i) {
                if (!recent.contains(primaryArtistKey(pool[rest[i]].t.artist))) { pick = i; break; }
            }
            const int idx = rest.takeAt(pick);
            ordered << idx;
            recent.push_back(primaryArtistKey(pool[idx].t.artist));
            if (recent.size() > gap) recent.removeFirst();
        }
        QStringList paths;
        for (int i : ordered) paths << pool[i].t.path;
        emit radioReady(title, paths);
    };

    // Collect candidates: each seed's own songs, plus songs from a few related artists.
    st->left = int(seeds.size());
    QSet<QString> seedIds;
    for (const auto& s : seeds) seedIds.insert(s.first.id);
    const int relatedPerSeed = seeds.size() > 2 ? 3 : 6;
    for (const auto& seed : seeds) {
        m_catalog->topTracks(seed.first.id, [st, seed, finish](const QVector<Track>& tracks) {
            for (const Track& t : tracks) st->cands.push_back({t, seed.second, true});
            if (--st->left == 0 && st->started) finish();
        });
    }
    st->left += int(seeds.size());                  // second wave: related artists, counted once per seed
    for (const auto& seed : seeds) {
        m_catalog->relatedArtists(seed.first.id, [this, st, seed, seedIds, relatedPerSeed, finish](const QVector<Db::CatArtist>& rel) {
            QVector<Db::CatArtist> use;
            for (const Db::CatArtist& a : rel) {
                if (seedIds.contains(a.id)) continue;
                use.push_back(a);
                if (use.size() >= relatedPerSeed) break;
            }
            if (use.isEmpty()) { if (--st->left == 0) finish(); return; }
            st->left += int(use.size()) - 1;
            for (int i = 0; i < use.size(); ++i) {
                const double rank = seed.second * (0.7 / (1.0 + 0.3 * i));
                const auto known = m_profile.constFind(primaryArtistKey(use[i].name));
                const bool familiar = known != m_profile.constEnd() && known->score > 0.15;
                m_catalog->topTracks(use[i].id, [st, rank, familiar, finish](const QVector<Track>& tracks) {
                    // the first 12 per related artist are plenty
                    for (int k = 0; k < tracks.size() && k < 12; ++k) st->cands.push_back({tracks[k], rank, familiar});
                    if (--st->left == 0) finish();
                });
            }
        });
    }
    st->started = true;
}

// ---- autoplay ----------------------------------------------------------------------------------

void Taste::autoplay(int ticket, const QStringList& seeds, const QStringList& exclude)
{
    constexpr int want = 12;
    struct Cand { Track t; double base; QString src; };
    struct State { QVector<Cand> c; int left = 1; };
    auto st = std::make_shared<State>();
    const int job = ++m_autoJob;
    const QSet<QString> skip(exclude.begin(), exclude.end());
    QSet<QString> seedKeys;
    QSet<QString> seedTitles;
    QVector<QPair<QSet<QString>, QString>> seedWords;        // (words of the title, artist key) of each seed
    QVector<Track> seedTracks;
    for (const QString& p : seeds) if (auto t = m_lib->trackByPath(p)) {
        seedTracks << *t;
        const QString titleKey = normTitleKey(t->title);
        seedKeys.insert(titleKey + QChar(0x1f) + primaryArtistKey(t->artist));
        if (!titleKey.isEmpty()) seedTitles.insert(titleKey);
        const QStringList ws = wordsOf(titleKey);
        seedWords.push_back({QSet<QString>(ws.begin(), ws.end()), primaryArtistKey(t->artist)});
    }

    auto finish = [this, st, job, want, ticket, skip, seedKeys, seedTitles, seedWords] {
        if (job != m_autoJob) return;
        const double popularity = m_params.value("popularity"), novelty = m_params.value("novelty"), likesW = m_params.value("likes");
        const double skipsW = m_params.value("skips"), diversity = m_params.value("diversity");
        const QStringList likedList = m_playlists->likedPaths();
        const QSet<QString> liked(likedList.begin(), likedList.end());
        QHash<QString, int> skipCount;
        for (const auto& s : Db::get().skips(QDateTime::currentSecsSinceEpoch() - 180LL * 86400)) skipCount[s.path]++;

        // one entry per recording (a local copy beats an online one), never something already queued
        QHash<QString, int> best;
        QVector<Cand> pool;
        for (const Cand& c : std::as_const(st->c)) {
            if (skip.contains(c.t.path)) continue;
            const QString k = normTitleKey(c.t.title) + QChar(0x1f) + primaryArtistKey(c.t.artist);
            // "Lucid Dreams" searches from web/catalog sources otherwise turn
            // into a queue of unrelated songs that merely reuse that title.
            // Apply this after all sources have contributed, not just to one
            // extension, so radio actually moves on to similar music.
            if (seedTitles.contains(normTitleKey(c.t.title))) continue;
            if (seedKeys.contains(k)) continue;
            // A different artist's song that merely shares most of the seed's words is a cover, a
            // re-upload or a coincidence, not "music like this": radio is about the music, not the title.
            {
                const QStringList cw = wordsOf(normTitleKey(c.t.title));
                const QString ck = primaryArtistKey(c.t.artist);
                bool lookalike = false;
                for (const auto& sw : seedWords) {
                    if (ck == sw.second || sw.first.size() < 2 || cw.size() < 2) continue;
                    int shared = 0;
                    for (const QString& w : cw) shared += sw.first.contains(w);
                    if (double(shared) / std::min<int>(cw.size(), sw.first.size()) >= 0.75) lookalike = true;
                }
                if (lookalike) continue;
            }
            const QString full = k + QChar(0x1f) + variantTag(c.t.title);
            const auto it = best.constFind(full);
            if (it == best.constEnd()) { best.insert(full, int(pool.size())); pool.push_back(c); }
            else if (!isCatalogPath(c.t.path) && !isExtPath(c.t.path) && (isCatalogPath(pool[*it].t.path) || isExtPath(pool[*it].t.path))) pool[*it] = c;
            else if (c.base > pool[*it].base) pool[*it].base = c.base;      // seen by several sources: stronger evidence
        }
        if (pool.isEmpty()) { emit autoplayReady(ticket, {}, {}); return; }
        QVector<double> w;
        for (const Cand& c : std::as_const(pool)) {
            const double pop = 1.0 + popularity * 2.0 * std::min(1.0, std::log10(1.0 + c.t.rank) / 6.0);
            const int plays = m_lib->stats()->count(c.t.path);
            const double nov = plays == 0 ? 1.0 + novelty : 1.0 / (1.0 + novelty * 0.4 * plays);
            const auto aff = m_profile.constFind(primaryArtistKey(c.t.artist));
            double x = c.base * pop * nov * (1.0 + 2.0 * (aff == m_profile.constEnd() ? 0.0 : aff->score));
            if (liked.contains(c.t.path)) x *= 1.0 + 0.3 * likesW;
            x /= 1.0 + skipsW * skipCount.value(c.t.path);
            w << std::max(0.001, x);
        }
        QRandomGenerator rng(quint32(QDateTime::currentMSecsSinceEpoch()));
        QVector<int> rest = weightedPick(w, want, rng);
        const int gap = std::max(1, int(std::round(diversity * 4)));
        QStringList paths, sources;
        QVector<QString> recent;
        while (!rest.isEmpty()) {
            int pick = 0;
            for (int i = 0; i < rest.size(); ++i)
                if (!recent.contains(primaryArtistKey(pool[rest[i]].t.artist))) { pick = i; break; }
            const int idx = rest.takeAt(pick);
            paths << pool[idx].t.path;
            sources << pool[idx].src;
            recent.push_back(primaryArtistKey(pool[idx].t.artist));
            if (recent.size() > gap) recent.removeFirst();
        }
        emit autoplayReady(ticket, paths, sources);
    };
    auto done = [st, finish] { if (--st->left == 0) finish(); };

    // 0. The song index: what you have played after this song, the artists next to it, and the artists
    //    behind your recent listening and searches. Instant (database only) and the main source of
    //    "music from your history".
    if (m_index) {
        for (const auto& p : m_index->candidates(seedTracks, 40)) {
            m_lib->noteRemote({p.t});                          // so the player can resolve it later
            st->c.push_back({p.t, 0.5 + 0.8 * std::min(2.2, p.w), QStringLiteral("history")});
        }
    }

    // 1. your own library
    for (int i = std::max(0, int(seedTracks.size()) - 2); i < seedTracks.size(); ++i) {
        const double recency = i == seedTracks.size() - 1 ? 1.0 : 0.6;
        for (const auto& pr : m_lib->similarLocal(seedTracks[i], 24)) st->c.push_back({pr.first, pr.second * recency * 1.2, QStringLiteral("library")});
    }
    if (seedTracks.isEmpty()) { done(); return; }
    const Track seed = seedTracks.last();

    // 2. extensions that can recommend
    if (m_plugins) {
        // Extensions that declare "recommend" are asked. Older installed copies of a source extension
        // (the yt-dlp one shipped without that word in its plugin.json) are asked too: one that
        // doesn't implement source.recommend just answers with an error and is skipped.
        QStringList askIds = m_plugins->enabledWith("recommend");
        for (const QString& id : m_plugins->enabledWith("source")) if (!askIds.contains(id)) askIds << id;
        for (const QString& id : std::as_const(askIds)) {
            ++st->left;
            const QString name = m_plugins->nameOf(id);
            m_plugins->call(id, "source.recommend",
                {{"title", seed.title}, {"artist", seed.artist}, {"album", seed.album}, {"limit", 25}},
                [this, st, id, name, done, seed](bool ok, const QJsonValue& v, const QString&) {
                    if (ok) {
                        QVector<Track> add;
                        const QJsonArray items = v.isArray() ? v.toArray() : v.toObject().value("items").toArray();
                        int rank = 0;
                        for (const QJsonValue& iv : items) {
                            const QJsonObject o = iv.toObject();
                            if (o.value("id").toString().isEmpty() || o.value("title").toString().isEmpty()) continue;
                            // A loose web "similar songs" query often returns covers and
                            // unrelated recordings which only share the seed title. They
                            // make Up Next look like a broken repetition rather than radio.
                            const QString candidateTitle = normTitleKey(o.value("title").toString());
                            const QString candidateArtist = primaryArtistKey(o.value("artist").toString());
                            if (candidateTitle == normTitleKey(seed.title) && candidateArtist != primaryArtistKey(seed.artist)) continue;
                            Track t;
                            t.path = extPath(id, o.value("id").toString());
                            // "Juice WRLD - Lucid Dreams (Official Video)" by "JuiceWRLDVEVO" becomes the
                            // real artist and title, so ranking, de-duplication and the local-copy
                            // check all work on proper names.
                            const UploadMeta um = parseUploadMeta(o.value("title").toString(), o.value("artist").toString());
                            t.title = um.title.isEmpty() ? o.value("title").toString() : um.title;
                            t.artist = um.artist.isEmpty() ? o.value("artist").toString() : um.artist;
                            t.album = o.value("album").toString();
                            t.durationMs = o.value("durationMs").toInteger();
                            t.coverUrl = o.value("cover").toString();
                            add << t;
                            st->c.push_back({t, 1.1 / (1.0 + 0.05 * rank++), name});
                        }
                        m_lib->noteRemote(add);
                    }
                    done();
                }, 20000);
        }
    }

    // 3. the online catalog: the seed's artist and the artists listeners move on to
    ++st->left;
    auto fromArtist = [this, st, done](const Db::CatArtist& a) {
        ++st->left;
        m_catalog->topTracks(a.id, [st, done](const QVector<Track>& tr) {
            for (int k = 0; k < tr.size() && k < 14; ++k) st->c.push_back({tr[k], 0.9, QStringLiteral("catalog")});
            done();
        });
        ++st->left;
        m_catalog->relatedArtists(a.id, [this, st, done](const QVector<Db::CatArtist>& rel) {
            for (int i = 0; i < rel.size() && i < 3; ++i) {
                ++st->left;
                const double w = 0.6 / (1.0 + 0.3 * i);
                m_catalog->topTracks(rel[i].id, [st, w, done](const QVector<Track>& tr) {
                    for (int k = 0; k < tr.size() && k < 8; ++k) st->c.push_back({tr[k], w, QStringLiteral("catalog")});
                    done();
                });
            }
            done();
        });
    };
    if (!seed.artistId.isEmpty()) {
        Db::CatArtist a;
        a.id = seed.artistId;
        a.name = seed.artist;
        fromArtist(a);
        done();
    } else {
        // An uploaded song's "artist" is a channel name ("Juice WRLD - Topic", "JuiceWRLDVEVO"):
        // clean it first or the catalog finds no such artist and the radio has nothing to branch from.
        const QString cleanName = stripUploaderNoise(seed.artist);
        m_catalog->resolveArtist(cleanName.isEmpty() ? seed.artist : cleanName, [fromArtist, done](const std::optional<Db::CatArtist>& a) {
            if (a) fromArtist(*a);
            done();
        });
    }
    done();      // the initial hold
}

// ---- export ------------------------------------------------------------------------------------

QString Taste::defaultExportName() const
{
    return QStringLiteral("lumen-algorithm-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
}

QJsonObject Taste::trackMeta(const Track& t) const
{
    return {{"id", catalogId(t.path)}, {"title", t.title}, {"artist", t.artist}, {"artistId", t.artistId}, {"album", t.album},
            {"albumId", t.albumId}, {"albumArtist", t.albumArtist}, {"cover", t.coverUrl}, {"durationMs", double(t.durationMs)},
            {"year", t.year}, {"rank", t.rank}};
}

QString Taste::exportTo(const QString& target)
{
    const QString file = localPath(target);
    if (file.isEmpty()) return tr("Choose where to save the file.");
    QJsonObject tracks;                                    // catalog songs referenced below, by id
    auto ref = [&](const QString& path) -> QJsonValue {
        const auto t = m_lib->trackByPath(path);
        if (!t) return QJsonValue();
        if (isCatalogPath(path)) {
            const QString id = catalogId(path);
            if (!tracks.contains(id)) tracks[id] = trackMeta(*t);
            return QStringLiteral("dz:") + id;
        }
        return QJsonObject{{"title", t->title}, {"artist", t->artist}, {"album", t->album}, {"durationMs", double(t->durationMs)}};
    };

    QJsonObject params;
    for (const ParamDef& d : kParams) params[d.key] = m_params.value(d.key);

    QJsonArray artists;
    QVector<Affinity> aff;
    for (const Affinity& a : m_profile) aff.push_back(a);
    std::sort(aff.begin(), aff.end(), [](const Affinity& a, const Affinity& b) { return a.score > b.score; });
    for (int i = 0; i < aff.size() && i < 300; ++i) {
        const auto known = Db::get().catArtistByName(aff[i].name);
        artists.append(QJsonObject{{"name", aff[i].name}, {"id", known ? known->id : QString()}, {"affinity", std::round(aff[i].score * 1000) / 1000}});
    }
    QJsonArray saved;
    for (const Db::CatArtist& a : Db::get().savedCatArtists()) saved.append(QJsonObject{{"id", a.id}, {"name", a.name}, {"picture", a.picture}});

    QJsonArray likes;
    for (const QString& p : m_playlists->likedPaths()) { const QJsonValue r = ref(p); if (!r.isNull()) likes.append(r); }

    QJsonArray playlists;
    for (const QVariant& v : m_playlists->list()) {
        const QVariantMap info = v.toMap();
        if (info.value("special").toString() == QLatin1String("liked")) continue;
        QJsonArray items;
        for (const QVariant& it : m_playlists->items(info.value("id").toString())) {
            const QJsonValue r = ref(it.toMap().value("path").toString());
            if (!r.isNull()) items.append(r);
        }
        playlists.append(QJsonObject{{"name", info.value("name").toString()}, {"items", items}});
    }

    QJsonArray history;
    const auto& ev = m_lib->stats()->events();
    for (int i = std::max(0, int(ev.size()) - 5000); i < ev.size(); ++i) {
        const QJsonValue r = ref(ev[i].path);
        if (!r.isNull()) history.append(QJsonObject{{"t", r}, {"at", double(ev[i].at)}});
    }
    QJsonArray skips;
    for (const auto& s : Db::get().skips(QDateTime::currentSecsSinceEpoch() - 365LL * 86400)) {
        const QJsonValue r = ref(s.path);
        if (!r.isNull()) skips.append(QJsonObject{{"t", r}, {"at", double(s.at)}});
    }

    const QJsonObject root{{"format", "lumen-algorithm"}, {"version", 1},
                           {"exported", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                           {"device", QSysInfo::machineHostName()},
                           {"params", params}, {"artists", artists}, {"savedArtists", saved}, {"likes", likes},
                           {"playlists", playlists}, {"history", history}, {"skips", skips}, {"tracks", tracks}};
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return tr("Couldn't write %1: %2").arg(file, f.errorString());
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit()) return tr("Couldn't save %1.").arg(file);
    return {};
}

// ---- import --------------------------------------------------------------------------------------

void Taste::importFrom(const QString& source)
{
    QFile f(localPath(source));
    if (!f.open(QIODevice::ReadOnly)) { emit importFailed(tr("Couldn't open that file.")); return; }
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (root.value("format").toString() != QLatin1String("lumen-algorithm")) { emit importFailed(tr("That isn't a Lumen algorithm file.")); return; }
    if (root.value("version").toInt() > 1) { emit importFailed(tr("This file was made by a newer version of Lumen.")); return; }

    setBusy(tr("Importing your algorithm…"));

    // 1. knobs
    const QJsonObject params = root.value("params").toObject();
    for (const ParamDef& d : kParams)
        if (params.contains(d.key)) m_params[d.key] = std::clamp(params.value(d.key).toDouble(d.def), d.min, d.max);
    saveParams();
    m_lib->stats()->setTauDays(m_params.value("recency"));
    emit paramsChanged();

    // 2. catalog songs the file refers to become known locally without any network traffic
    const QJsonObject tracks = root.value("tracks").toObject();
    QVector<Track> known;
    for (auto it = tracks.begin(); it != tracks.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Track t;
        t.path = catalogPath(it.key());
        t.title = o.value("title").toString(); t.artist = o.value("artist").toString(); t.artistId = o.value("artistId").toString();
        t.album = o.value("album").toString(); t.albumId = o.value("albumId").toString(); t.albumArtist = o.value("albumArtist").toString();
        t.coverUrl = o.value("cover").toString(); t.coverKey = t.coverUrl; t.durationMs = qint64(o.value("durationMs").toDouble());
        t.year = o.value("year").toInt(); t.rank = o.value("rank").toInt(); t.codec = QStringLiteral("Stream");
        if (!t.title.isEmpty()) known.push_back(t);
    }
    m_lib->noteRemote(known);

    // 3. turn a reference ("dz:123" or {title, artist...}) into a path on this device
    int unmatched = 0;
    auto toPath = [&](const QJsonValue& v) -> QString {
        if (v.isString()) { const QString s = v.toString(); return s.startsWith(QLatin1String("dz:")) ? catalogPath(s.mid(3)) : QString(); }
        const QJsonObject o = v.toObject();
        if (const auto l = m_lib->matchLocal(o.value("title").toString(), o.value("artist").toString(), qint64(o.value("durationMs").toDouble()))) return l->path;
        ++unmatched;
        return {};
    };

    // 4. history + skips (skipping what is already recorded, so importing twice is harmless)
    QSet<QString> have;
    for (const auto& e : m_lib->stats()->events()) have.insert(e.path + QChar(0x1f) + QString::number(e.at));
    int plays = 0;
    Db& db = Db::get();
    db.begin();
    for (const QJsonValue& v : root.value("history").toArray()) {
        const QJsonObject o = v.toObject();
        const QString p = toPath(o.value("t"));
        const qint64 at = qint64(o.value("at").toDouble());
        if (p.isEmpty() || at <= 0 || have.contains(p + QChar(0x1f) + QString::number(at))) continue;
        db.recordPlay(p, at, 30000);
        ++plays;
    }
    QSet<QString> haveSkip;
    for (const auto& s : db.skips(0)) haveSkip.insert(s.path + QChar(0x1f) + QString::number(s.at));
    for (const QJsonValue& v : root.value("skips").toArray()) {
        const QJsonObject o = v.toObject();
        const QString p = toPath(o.value("t"));
        const qint64 at = qint64(o.value("at").toDouble());
        if (p.isEmpty() || at <= 0 || haveSkip.contains(p + QChar(0x1f) + QString::number(at))) continue;
        db.recordSkip(p, at);
    }
    db.commit();
    m_lib->stats()->reloadFromDb();

    // 5. likes + playlists
    QStringList likePaths;
    for (const QJsonValue& v : root.value("likes").toArray()) { const QString p = toPath(v); if (!p.isEmpty()) likePaths << p; }
    m_playlists->add(m_playlists->likedId(), likePaths);
    int lists = 0;
    for (const QJsonValue& v : root.value("playlists").toArray()) {
        const QJsonObject o = v.toObject();
        QStringList paths;
        for (const QJsonValue& it : o.value("items").toArray()) { const QString p = toPath(it); if (!p.isEmpty()) paths << p; }
        const QString name = o.value("name").toString();
        QString existing;
        for (const QVariant& pv : m_playlists->list()) if (pv.toMap().value("name").toString() == name && pv.toMap().value("special").toString().isEmpty()) existing = pv.toMap().value("id").toString();
        if (!existing.isEmpty()) m_playlists->add(existing, paths); else m_playlists->create(name, paths);
        ++lists;
    }

    // 6. saved artists: bring their whole catalog back (one at a time, the catalog is paced)
    struct Todo { QVector<QPair<QString, QString>> artists; int done = 0, songs = 0; };
    auto todo = std::make_shared<Todo>();
    for (const QJsonValue& v : root.value("savedArtists").toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        if (id.isEmpty() || m_catalog->isSavedArtist(id)) continue;
        Db::get().upsertCatArtist({id, o.value("name").toString(), o.value("picture").toString(), 0, false}, false);
        todo->artists.push_back({id, o.value("name").toString()});
    }
    const int job = ++m_job;
    auto summary = [this, plays, lists, unmatched, likes = likePaths.size(), todo] {
        refreshProfile();
        setBusy({});
        QStringList parts;
        parts << tr("%1 plays").arg(plays);
        if (likes) parts << tr("%1 likes").arg(likes);
        if (lists) parts << tr("%1 playlists").arg(lists);
        if (!todo->artists.isEmpty()) parts << tr("%1 artists (%2 songs)").arg(todo->artists.size()).arg(todo->songs);
        QString s = tr("Imported ") + parts.join(QStringLiteral(", ")) + QLatin1Char('.');
        if (unmatched > 0) s += tr(" %1 songs from your files weren't found on this device.").arg(unmatched);
        emit importFinished(s);
    };
    m_importStep = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = m_importStep;
    *m_importStep = [this, todo, summary, job, weak]() {
        if (job != m_job || todo->done >= todo->artists.size()) {
            summary();
            QTimer::singleShot(0, this, [this] { m_importStep.reset(); });
            return;
        }
        const auto a = todo->artists[todo->done];
        setBusy(tr("Adding %1 (%2 of %3)\u2026").arg(a.second).arg(todo->done + 1).arg(todo->artists.size()));
        m_catalog->importArtistQuiet(a.first, [todo, weak](int n) {
            todo->songs += n;
            todo->done++;
            if (auto s = weak.lock()) (*s)();
        });
    };
    (*m_importStep)();
}
