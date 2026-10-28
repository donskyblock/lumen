// Search ranking, artist pages and the home-page recommendations.
#include "Library.h"
#include "TextUtil.h"
#include <QDate>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace {
inline bool cmp3(const QString& a, const QString& b) { return QString::compare(a, b, Qt::CaseInsensitive) < 0; }
struct Fields { QString title, artist, album, genre; };
Fields splitFolded(const QString& f)
{
    const QStringList p = f.split(QChar(0x1f));
    return {p.value(0), p.value(1), p.value(2), p.value(3)};
}

// All tokens must match; each contributes the weight of its best field.
double scoreTokens(const QStringList& tokens, const Fields& f)
{
    double total = 0;
    for (const QString& tok : tokens) {
        double best = 0;
        if (hasWordPrefix(f.title, tok)) best = 6; else if (f.title.contains(tok)) best = 4;
        if (hasWordPrefix(f.artist, tok)) best = std::max(best, 5.0); else if (f.artist.contains(tok)) best = std::max(best, 3.0);
        if (hasWordPrefix(f.album, tok)) best = std::max(best, 3.0); else if (f.album.contains(tok)) best = std::max(best, 2.0);
        if (!f.genre.isEmpty() && f.genre.contains(tok)) best = std::max(best, 1.0);
        if (best == 0) return -1;
        total += best;
    }
    return total;
}

// Typo-tolerant fallback: a token may match a word within 1-2 edits.
double fuzzyTokens(const QStringList& tokens, const Fields& f)
{
    double total = 0;
    const QStringList words = wordsOf(f.title) + wordsOf(f.artist) + wordsOf(f.album);
    for (const QString& tok : tokens) {
        const double exact = scoreTokens({tok}, f);
        if (exact > 0) { total += exact; continue; }
        if (tok.size() < 3) return -1;
        const int maxD = tok.size() >= 8 ? 2 : 1;
        bool hit = false;
        for (const QString& w : words)
            if (boundedEditDistance(tok, w.left(tok.size() + 1), maxD) <= maxD || boundedEditDistance(tok, w, maxD) <= maxD) { hit = true; break; }
        if (!hit) return -1;
        total += 1.5;
    }
    return total;
}

bool fuzzyWord(const QString& tok, const QStringList& words)
{
    const int maxD = tok.size() >= 8 ? 2 : 1;
    for (const QString& w : words) if (boundedEditDistance(tok, w, maxD) <= maxD) return true;
    return false;
}

QVector<int> weightedSample(const QVector<QPair<int, double>>& items, int n, QRandomGenerator& rng)
{
    QVector<QPair<double, int>> keyed;
    keyed.reserve(items.size());
    for (const auto& p : items) {
        double u = rng.generateDouble();
        if (u <= 1e-12) u = 1e-12;
        keyed.push_back({std::pow(u, 1.0 / std::max(1e-6, p.second)), p.first});
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QVector<int> out;
    for (int i = 0; i < keyed.size() && i < n; ++i) out.push_back(keyed[i].second);
    return out;
}
} // namespace

// ---- search ---------------------------------------------------------------------------

QVariantMap Library::searchAll(const QString& query, bool losslessOnly) const
{
    QVariantMap res{{"songs", QVariantList()}, {"albums", QVariantList()}, {"artists", QVariantList()}, {"fuzzy", false}};
    const QString q = foldText(query).simplified();
    const QStringList tokens = q.split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return res;

    const auto& tr = m_tracks.tracks();
    const auto& al = m_albums.albums();
    const auto& ar = m_artists.artists();

    struct Hit { double score; int idx; };
    auto run = [&](bool fuzzy, QVector<Hit>& songs, QVector<Hit>& albums, QVector<Hit>& artists) {
        for (int i = 0; i < tr.size(); ++i) {
            if (losslessOnly && !tr[i].lossless) continue;
            const Fields f = splitFolded(tr[i].folded);
            double s = fuzzy ? fuzzyTokens(tokens, f) : scoreTokens(tokens, f);
            if (s < 0) continue;
            if (f.title == q) s += 20; else if (f.title.startsWith(q)) s += 8;
            s += 0.6 * std::log1p(m_stats.count(tr[i].path));
            songs.push_back({s, i});
        }
        for (int i = 0; i < al.size(); ++i) {
            if (losslessOnly && !al[i].lossless) continue;
            const QStringList p = al[i].folded.split(QChar(0x1f));
            const Fields f{p.value(0), p.value(1), {}, {}};
            Fields swapped{f.title, f.artist, {}, {}};
            double s = fuzzy ? fuzzyTokens(tokens, swapped) : scoreTokens(tokens, swapped);
            if (s < 0) continue;
            if (f.title == q) s += 15;
            albums.push_back({s, i});
        }
        for (int i = 0; i < ar.size(); ++i) {
            const QString& k = ar[i].key;
            double s = 0;
            bool ok = true;
            const QStringList kWords = wordsOf(k);
            for (const QString& tok : tokens) {
                if (hasWordPrefix(k, tok)) s += 8;
                else if (k.contains(tok)) s += 5;
                else if (fuzzy && tok.size() >= 3 && fuzzyWord(tok, kWords)) s += 2;
                else { ok = false; break; }
            }
            if (!ok) continue;
            if (k == q) s += 25;
            s += std::min(3.0, std::sqrt(double(ar[i].tracks.size())) * 0.3);
            artists.push_back({s, i});
        }
    };

    QVector<Hit> songs, albums, artists;
    run(false, songs, albums, artists);
    bool fuzzy = false;
    if (songs.size() + albums.size() + artists.size() < 3) {      // nothing close: forgive typos
        songs.clear(); albums.clear(); artists.clear();
        run(true, songs, albums, artists);
        fuzzy = true;
    }
    auto byScore = [](const Hit& a, const Hit& b) { return a.score > b.score; };
    std::stable_sort(songs.begin(), songs.end(), byScore);
    std::stable_sort(albums.begin(), albums.end(), byScore);
    std::stable_sort(artists.begin(), artists.end(), byScore);

    QVariantList sl, bl, rl;
    for (int i = 0; i < songs.size() && i < 30; ++i) sl << trackMap(tr[songs[i].idx]);
    for (int i = 0; i < albums.size() && i < 14; ++i) {
        const Album& a = al[albums[i].idx];
        bl << QVariantMap{{"key", a.key}, {"title", a.title}, {"artist", a.artist}, {"cover", a.coverKey}, {"year", a.year},
                          {"lossless", a.lossless}, {"count", a.tracks.size()}};
    }
    for (int i = 0; i < artists.size() && i < 14; ++i) {
        const Artist& a = ar[artists[i].idx];
        rl << QVariantMap{{"name", a.name}, {"cover", a.coverKey}, {"albumCount", a.albums.size()}, {"count", a.tracks.size()}};
    }
    res["songs"] = sl; res["albums"] = bl; res["artists"] = rl; res["fuzzy"] = fuzzy;

    // Top result: an exact-ish artist or album name beats a song; otherwise the best song.
    QVariantMap top;
    const double bs = songs.isEmpty() ? -1 : songs.first().score;
    const double ba = albums.isEmpty() ? -1 : albums.first().score;
    const double br = artists.isEmpty() ? -1 : artists.first().score;
    if (br >= 0 && (br >= 20 || (br >= bs && br >= ba))) { top = rl.first().toMap(); top["kind"] = "artist"; }
    else if (ba >= 0 && ba >= bs + 2) { top = bl.first().toMap(); top["kind"] = "album"; }
    else if (bs >= 0) { top = sl.first().toMap(); top["kind"] = "song"; }
    else if (ba >= 0) { top = bl.first().toMap(); top["kind"] = "album"; }
    res["top"] = top;
    return res;
}

// ---- top result: local and online songs, mixed fairly ----------------------------------------

QVariantList Library::blendSongs(const QString& query, const QVariantList& local, const QVariantList& online, int n) const
{
    const QString wantTitle = normTitleKey(query);
    auto key = [](const QVariantMap& m) {
        return normTitleKey(m.value("title").toString()) + QChar(0x1f) + primaryArtistKey(m.value("artist").toString())
             + QChar(0x1f) + variantTag(m.value("title").toString());
    };
    auto isExact = [&](const QVariantMap& m) { return !wantTitle.isEmpty() && normTitleKey(m.value("title").toString()) == wantTitle; };

    QVector<QVariantMap> loc, onl;
    QSet<QString> localKeys, localPaths;
    for (const QVariant& v : local) {
        QVariantMap m = v.toMap();
        localKeys.insert(key(m));
        localPaths.insert(m.value("path").toString());
        if (loc.size() < 12) loc.push_back(m);
    }
    for (const QVariant& v : online) {
        QVariantMap m = v.toMap();
        // the same recording as one you own (same title, artist and edition) is never offered twice:
        // the copy on your disk is the one that plays
        if (localKeys.contains(key(m)) || localPaths.contains(m.value("path").toString())) continue;
        if (onl.size() < 12) onl.push_back(m);
    }
    // "Most played": local songs by your play count, online songs by the catalog's popularity rank.
    // A song whose title is exactly what was typed always goes first within its side.
    std::stable_sort(loc.begin(), loc.end(), [&](const QVariantMap& a, const QVariantMap& b) {
        const bool ea = isExact(a), eb = isExact(b);
        if (ea != eb) return ea;
        return a.value("plays").toInt() > b.value("plays").toInt();
    });
    std::stable_sort(onl.begin(), onl.end(), [&](const QVariantMap& a, const QVariantMap& b) {
        const bool ea = isExact(a), eb = isExact(b);
        if (ea != eb) return ea;
        return a.value("rank").toInt() > b.value("rank").toInt();
    });
    // Alternate the two sides. Your library leads unless only the online side has the exact title.
    const bool onlineFirst = !onl.isEmpty() && isExact(onl.first()) && (loc.isEmpty() || !isExact(loc.first()));
    QVariantList out;
    int li = 0, oi = 0;
    bool takeLocal = !onlineFirst;
    while (out.size() < n && (li < loc.size() || oi < onl.size())) {
        if ((takeLocal && li < loc.size()) || oi >= onl.size()) { QVariantMap m = loc[li++]; m["online"] = false; out << m; }
        else { QVariantMap m = onl[oi++]; m["online"] = true; out << m; }
        takeLocal = !takeLocal;
    }
    return out;
}

// ---- artists ---------------------------------------------------------------------------

QVector<QPair<QString, double>> Library::relatedArtists(const QString& key, int n) const
{
    QHash<QString, double> score;
    const int self = m_artistByKey.value(key, -1);
    if (self < 0) return {};
    const auto& ar = m_artists.artists();
    // 1. shared genres
    const QSet<QString>& g = ar[self].genres;
    if (!g.isEmpty()) {
        for (int i = 0; i < ar.size(); ++i) {
            if (i == self || ar[i].genres.isEmpty()) continue;
            const int inter = QSet<QString>(g).intersect(ar[i].genres).size();
            if (!inter) continue;
            score[ar[i].key] += 3.0 * inter / double(g.size() + ar[i].genres.size() - inter);
        }
    }
    // 2. played close together in time
    const auto& ev = m_stats.events();
    auto artistsOf = [&](const QString& path) {
        QStringList out;
        if (const auto t = trackByPath(path))
            for (const QString& nme : splitArtists(t->artist) + splitArtists(t->albumArtist)) {
                const QString k = foldText(nme);
                if (m_artistByKey.contains(k) && !out.contains(k)) out << k;
            }
        return out;
    };
    for (int i = 0; i < ev.size(); ++i) {
        const QStringList a = artistsOf(ev[i].path);
        for (int j = i - 1; j >= 0 && j >= i - 10 && ev[i].at - ev[j].at <= 1200; --j) {
            const QStringList b = artistsOf(ev[j].path);
            for (const QString& x : a) for (const QString& y : b) {
                if (x == y) continue;
                if (x == key) score[y] += 1.0; else if (y == key) score[x] += 1.0;
            }
        }
    }
    QVector<QPair<QString, double>> out;
    for (auto it = score.cbegin(); it != score.cend(); ++it) out.push_back({it.key(), it.value()});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (out.size() > n) out.resize(n);
    return out;
}

QVariantMap Library::artistPage(const QString& name) const
{
    const int ai = m_artistByKey.value(foldText(name), -1);
    if (ai < 0) return {};
    const Artist& a = m_artists.artists().at(ai);
    const auto& tr = m_tracks.tracks();
    const auto& al = m_albums.albums();

    auto albumList = [&](const QVector<int>& idx) {
        QVector<int> sorted = idx;
        std::sort(sorted.begin(), sorted.end(), [&](int x, int y) {
            if (al[x].year != al[y].year) return al[x].year > al[y].year;
            return cmp3(al[x].title, al[y].title);
        });
        QVariantList out;
        for (int i : sorted)
            out << QVariantMap{{"key", al[i].key}, {"title", al[i].title}, {"artist", al[i].artist}, {"cover", al[i].coverKey},
                               {"year", al[i].year}, {"lossless", al[i].lossless}, {"count", al[i].tracks.size()}};
        return out;
    };

    // popular: most-played first; before there's any history, just the first few songs
    QVector<int> byScore = a.tracks;
    std::stable_sort(byScore.begin(), byScore.end(), [&](int x, int y) { return m_stats.score(tr[x].path) > m_stats.score(tr[y].path); });
    const bool hasPlays = !byScore.isEmpty() && m_stats.score(tr[byScore.first()].path) > 0;
    QVariantList popular;
    for (int i = 0; i < byScore.size() && i < 5; ++i) popular << trackMap(tr[byScore[i]]);

    QSet<QString> genres;
    for (int i : a.tracks) for (const QString& g : tr[i].genre.split(QRegularExpression("[;,/]"), Qt::SkipEmptyParts)) genres.insert(g.trimmed());

    QVariantList related;
    for (const auto& r : relatedArtists(a.key, 8)) {
        const int ri = m_artistByKey.value(r.first, -1);
        if (ri >= 0) related << QVariantMap{{"name", m_artists.artists()[ri].name}, {"cover", m_artists.artists()[ri].coverKey},
                                            {"albumCount", m_artists.artists()[ri].albums.size()}, {"count", m_artists.artists()[ri].tracks.size()}};
    }
    int plays = 0;
    for (int i : a.tracks) plays += m_stats.count(tr[i].path);
    return {{"name", a.name}, {"cover", a.coverKey}, {"albumCount", a.albums.size()}, {"trackCount", a.tracks.size()},
            {"durationMs", a.durationMs}, {"losslessPct", a.tracks.isEmpty() ? 0 : 100 * a.losslessTracks / a.tracks.size()},
            {"genres", QStringList(genres.begin(), genres.end())}, {"albums", albumList(a.albums)},
            {"appearsOn", albumList(a.appearsOn)}, {"popular", popular}, {"hasPlays", hasPlays}, {"plays", plays},
            {"related", related}};
}

QVector<QPair<Track, double>> Library::similarLocal(const Track& seed, int n) const
{
    QVector<QPair<Track, double>> out;
    const auto& tr = m_tracks.tracks();
    QRandomGenerator& rng = *QRandomGenerator::global();
    QSet<int> used;
    auto addFrom = [&](const QVector<int>& idx, double weight, int cap) {
        QVector<int> pick = idx;
        std::shuffle(pick.begin(), pick.end(), rng);
        int added = 0;
        for (int i : pick) {
            if (added >= cap || out.size() >= n * 3) break;
            if (tr[i].path == seed.path || used.contains(i)) continue;
            used.insert(i);
            out.push_back({tr[i], weight});
            ++added;
        }
    };
    const QStringList names = splitArtists(seed.artist);
    const QString firstKey = names.isEmpty() ? foldText(seed.artist) : foldText(names.first());
    const int self = m_artistByKey.value(firstKey, -1);
    if (self >= 0) {
        addFrom(m_artists.artists()[self].tracks, 1.0, 14);
        int rank = 0;
        for (const auto& r : relatedArtists(firstKey, 6)) {
            const int ri = m_artistByKey.value(r.first, -1);
            if (ri >= 0) addFrom(m_artists.artists()[ri].tracks, 0.6 / (1.0 + 0.3 * rank), 8);
            ++rank;
        }
    }
    if (!seed.genre.isEmpty()) {                                           // same genre, any artist
        QVector<int> same;
        const QString g = foldText(seed.genre.split(QRegularExpression("[;,/]"), Qt::SkipEmptyParts).value(0).trimmed());
        for (int i = 0; i < tr.size(); ++i)
            if (!g.isEmpty() && foldText(tr[i].genre).contains(g)) same.push_back(i);
        addFrom(same, 0.3, 12);
    }
    if (out.size() > n * 3) out.resize(n * 3);
    return out;
}

QStringList Library::artistPaths(const QString& name) const
{
    QStringList out;
    const int ai = m_artistByKey.value(foldText(name), -1);
    if (ai < 0) return out;
    const auto& tr = m_tracks.tracks();
    QVector<int> idx = m_artists.artists()[ai].tracks;
    std::stable_sort(idx.begin(), idx.end(), [&](int x, int y) { return m_stats.score(tr[x].path) > m_stats.score(tr[y].path); });
    for (int i : idx) out << tr[i].path;
    return out;
}

// ---- home page: mixes -----------------------------------------------------------------------

QVariantMap Library::buildMix(const QString& id) const
{
    const auto& tr = m_tracks.tracks();
    const auto& ar = m_artists.artists();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QRandomGenerator rng(quint32(QDate::currentDate().toJulianDay()) * 2654435761u ^ quint32(qHash(id)));    // stable for the day

    QVector<QPair<int, double>> cand;
    QString title, subtitle;
    int want = 30;

    // artists ranked by how much you've listened lately
    auto topArtists = [&] {
        QHash<int, double> sc;
        for (int i = 0; i < tr.size(); ++i) {
            const double s = m_stats.score(tr[i].path);
            if (s <= 0) continue;
            QSet<int> seen;
            for (const QString& n : splitArtists(tr[i].artist) + splitArtists(tr[i].albumArtist)) {
                const int a = m_artistByKey.value(foldText(n), -1);
                if (a >= 0 && !seen.contains(a)) { seen.insert(a); sc[a] += s; }
            }
        }
        QVector<QPair<int, double>> v;
        for (auto it = sc.cbegin(); it != sc.cend(); ++it) v.push_back({it.key(), it.value()});
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        return v;
    };

    if (id.startsWith("daily")) {
        const int n = id.mid(5).toInt() - 1;
        const auto top = topArtists();
        if (n < 0 || n >= top.size()) return {};
        const Artist& seed = ar[top[n].first];
        const auto rel = relatedArtists(seed.key, 5);
        QSet<int> relIdx;
        QStringList names{seed.name};
        for (const auto& r : rel) { const int ri = m_artistByKey.value(r.first, -1); if (ri >= 0) { relIdx.insert(ri); if (names.size() < 3) names << ar[ri].name; } }
        for (int i = 0; i < tr.size(); ++i) {
            double w = 0;
            for (const QString& nme : splitArtists(tr[i].artist) + splitArtists(tr[i].albumArtist)) {
                const int a = m_artistByKey.value(foldText(nme), -1);
                if (a == top[n].first) w = std::max(w, 3.0);
                else if (relIdx.contains(a)) w = std::max(w, 1.0);
            }
            if (w > 0) cand.push_back({i, w});
        }
        title = QStringLiteral("Daily Mix %1").arg(n + 1);
        subtitle = names.join(", ") + (rel.size() > 2 ? QStringLiteral(" and more") : QString());
    } else if (id == "discovery") {
        const auto top = topArtists();
        QSet<int> liked;
        for (int i = 0; i < top.size() && i < 8; ++i) liked.insert(top[i].first);
        for (int i = 0; i < tr.size(); ++i) {
            if (m_stats.count(tr[i].path) > 0) continue;
            double w = 1;
            for (const QString& nme : splitArtists(tr[i].artist))
                if (liked.contains(m_artistByKey.value(foldText(nme), -1))) w = 3;
            cand.push_back({i, w});
        }
        title = QStringLiteral("Discovery Mix");
        subtitle = QStringLiteral("Songs you haven't played yet");
    } else if (id == "forgotten") {
        for (int pass = 0; pass < 2 && cand.isEmpty(); ++pass) {
            const int minCount = pass == 0 ? 2 : 1;
            const qint64 age = (pass == 0 ? 21 : 7) * 86400LL;
            for (int i = 0; i < tr.size(); ++i) {
                const int c = m_stats.count(tr[i].path);
                if (c >= minCount && now - m_stats.last(tr[i].path) > age) cand.push_back({i, 1.0 + c});
            }
        }
        title = QStringLiteral("Forgotten Favorites");
        subtitle = QStringLiteral("Songs you loved, a while ago");
    } else if (id == "lossless") {
        for (int i = 0; i < tr.size(); ++i)
            if (tr[i].lossless) cand.push_back({i, std::max(0.5, (tr[i].sampleRate / 44100.0) * (std::max(16, tr[i].bitDepth) / 16.0))});
        title = QStringLiteral("Lossless Selects");
        subtitle = QStringLiteral("Your best-sounding files");
    } else if (id.startsWith("genre:")) {
        const QString g = id.mid(6);
        for (int i = 0; i < tr.size(); ++i)
            for (const QString& x : tr[i].genre.split(QRegularExpression("[;,/]"), Qt::SkipEmptyParts))
                if (foldText(x.trimmed()) == g) { cand.push_back({i, 1.0}); break; }
        QString display = g;
        if (!cand.isEmpty()) display = tr[cand.first().first].genre.split(QRegularExpression("[;,/]"), Qt::SkipEmptyParts).value(0).trimmed();
        title = display + QStringLiteral(" Mix");
        subtitle = QStringLiteral("From your library");
    } else if (id == "shuffle") {
        for (int i = 0; i < tr.size(); ++i) cand.push_back({i, 1.0});
        title = QStringLiteral("Library Shuffle");
        subtitle = QStringLiteral("A bit of everything");
    } else return {};

    if (cand.size() < 6) return {};
    const QVector<int> picked = weightedSample(cand, want, rng);
    QStringList paths, covers;
    QSet<QString> seenCover;
    for (int i : picked) {
        paths << tr[i].path;
        if (covers.size() < 4 && !seenCover.contains(tr[i].album)) { seenCover.insert(tr[i].album); covers << tr[i].coverKey; }
    }
    return {{"id", id}, {"title", title}, {"subtitle", subtitle}, {"covers", covers}, {"paths", paths}, {"count", paths.size()}};
}

QStringList Library::mixPaths(const QString& id) const { return buildMix(id).value("paths").toStringList(); }

QVariantMap Library::home() const
{
    const auto& tr = m_tracks.tracks();
    const auto& al = m_albums.albums();
    const auto& ar = m_artists.artists();
    auto albumMap = [&](const Album& a) {
        return QVariantMap{{"key", a.key}, {"title", a.title}, {"artist", a.artist}, {"cover", a.coverKey}, {"lossless", a.lossless},
                           {"count", a.tracks.size()}, {"year", a.year}};
    };

    QVariantMap out;
    // recently played albums, newest first
    QVariantList recent;
    QSet<int> seenAlbum;
    const auto& ev = m_stats.events();
    QHash<QString, int> idxByPath;
    for (int i = 0; i < tr.size(); ++i) idxByPath.insert(tr[i].path, i);
    for (int i = ev.size() - 1; i >= 0 && recent.size() < 12; --i) {
        const int ti = idxByPath.value(ev[i].path, -1);
        if (ti < 0) continue;
        const int ai = m_trackAlbum.value(ti, -1);
        if (ai < 0 || seenAlbum.contains(ai)) continue;
        seenAlbum.insert(ai);
        recent << albumMap(al[ai]);
    }
    out["recent"] = recent;

    // recently added
    QVector<int> idx(al.size());
    for (int i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return al[a].added > al[b].added; });
    QVariantList added;
    for (int i = 0; i < idx.size() && i < 12; ++i) added << albumMap(al[idx[i]]);
    out["added"] = added;

    // most played songs + artists
    QVector<int> byScore;
    for (int i = 0; i < tr.size(); ++i) if (m_stats.score(tr[i].path) > 0) byScore << i;
    std::sort(byScore.begin(), byScore.end(), [&](int a, int b) { return m_stats.score(tr[a].path) > m_stats.score(tr[b].path); });
    QVariantList top;
    for (int i = 0; i < byScore.size() && i < 10; ++i) top << trackMap(tr[byScore[i]]);
    out["topTracks"] = top;

    QHash<int, double> asc;
    for (int i : byScore) {
        QSet<int> seen;
        for (const QString& n : splitArtists(tr[i].artist) + splitArtists(tr[i].albumArtist)) {
            const int a = m_artistByKey.value(foldText(n), -1);
            if (a >= 0 && !seen.contains(a)) { seen.insert(a); asc[a] += m_stats.score(tr[i].path); }
        }
    }
    QVector<QPair<int, double>> av;
    for (auto it = asc.cbegin(); it != asc.cend(); ++it) av.push_back({it.key(), it.value()});
    std::sort(av.begin(), av.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QVariantList topArtists;
    for (int i = 0; i < av.size() && i < 12; ++i)
        topArtists << QVariantMap{{"name", ar[av[i].first].name}, {"cover", ar[av[i].first].coverKey},
                                  {"albumCount", ar[av[i].first].albums.size()}, {"count", ar[av[i].first].tracks.size()}};
    // before there's history, still offer artists so the shelf isn't empty
    for (int i = 0; i < ar.size() && topArtists.isEmpty() && i < 12; ++i)
        out["artistsFallback"] = true, topArtists << QVariantMap{{"name", ar[i].name}, {"cover", ar[i].coverKey}, {"albumCount", ar[i].albums.size()}, {"count", ar[i].tracks.size()}};
    out["topArtists"] = topArtists;

    // mixes
    QStringList ids;
    if (m_stats.any()) ids << "daily1" << "daily2" << "daily3" << "discovery" << "forgotten" << "lossless";
    else {
        QHash<QString, int> gcount;
        for (const Track& t : tr)
            for (const QString& g : t.genre.split(QRegularExpression("[;,/]"), Qt::SkipEmptyParts)) gcount[foldText(g.trimmed())]++;
        QVector<QPair<QString, int>> gv;
        for (auto it = gcount.cbegin(); it != gcount.cend(); ++it) if (!it.key().isEmpty()) gv.push_back({it.key(), it.value()});
        std::sort(gv.begin(), gv.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (int i = 0; i < gv.size() && i < 3; ++i) ids << "genre:" + gv[i].first;
        ids << "discovery" << "lossless" << "shuffle";
    }
    QVariantList mixes;
    for (const QString& id : ids) {
        QVariantMap m = buildMix(id);
        if (m.isEmpty()) continue;
        m.remove("paths");
        mixes << m;
    }
    out["mixes"] = mixes;
    out["hasHistory"] = m_stats.any();
    return out;
}
