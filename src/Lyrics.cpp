#include "Lyrics.h"
#include "Db.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

Lyrics::Lyrics(PluginManager* plugins, QObject* parent) : QObject(parent), m_plugins(plugins)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(350);       // skipping through tracks shouldn't hit providers
    connect(&m_debounce, &QTimer::timeout, this, [this] { run(true); });
}

// ---- text helpers --------------------------------------------------------------------

QString Lyrics::normalise(const QString& in)
{
    static const QRegularExpression brackets(R"([\(\[][^\)\]]*[\)\]])");
    static const QRegularExpression suffix(R"(\s[-\x{2013}]\s(remaster(ed)?|live|radio edit|single version|mono|stereo|version|mix|edit|\d{4}).*$)",
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression junk(R"([^\p{L}\p{N}\s])");
    QString s = in.toLower();
    s.remove(brackets);
    s.remove(suffix);
    s.replace('&', " and ");
    s.remove(junk);
    return s.simplified();
}

double Lyrics::similarity(const QString& a, const QString& b)
{
    const QString x = normalise(a), y = normalise(b);
    if (x.isEmpty() || y.isEmpty()) return 0;
    if (x == y) return 1;
    const QStringList xl = x.split(' '), yl = y.split(' ');
    const QSet<QString> tx(xl.begin(), xl.end());
    const QSet<QString> ty(yl.begin(), yl.end());
    const int inter = QSet<QString>(tx).intersect(ty).size();
    const double jaccard = double(inter) / double(tx.size() + ty.size() - inter);
    const double contain = double(inter) / double(std::min(tx.size(), ty.size()));
    double s = 0.6 * jaccard + 0.4 * contain;
    if (x.contains(y) || y.contains(x)) s = std::max(s, 0.8);
    return std::min(1.0, s);
}

QVariantList Lyrics::parseLrc(const QString& text, bool* synced)
{
    static const QRegularExpression ts(R"(\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\])");
    static const QRegularExpression offsetTag(R"(^\[offset:\s*([+-]?\d+)\s*\]$)", QRegularExpression::CaseInsensitiveOption);
    struct L { qint64 t; QString s; };
    QVector<L> out;
    qint64 offset = 0;
    bool any = false;
    for (const QString& raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (auto m = offsetTag.match(line); m.hasMatch()) { offset = m.captured(1).toLongLong(); continue; }
        QVector<qint64> stamps;
        int end = 0;
        for (auto it = ts.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            if (m.capturedStart() != end) break;       // only leading stamps
            end = m.capturedEnd();
            QString frac = m.captured(3);
            qint64 ms = 0;
            if (!frac.isEmpty()) ms = frac.leftJustified(3, '0').left(3).toInt();
            stamps << m.captured(1).toLongLong() * 60000 + m.captured(2).toLongLong() * 1000 + ms;
        }
        if (stamps.isEmpty()) {
            if (!line.startsWith('[')) out.push_back({-1, line});     // plain line
            continue;                                                  // [ar:..] style tags
        }
        any = true;
        const QString body = line.mid(end).trimmed();
        for (qint64 t : stamps) out.push_back({std::max<qint64>(0, t - offset), body});
    }
    if (any) {
        out.erase(std::remove_if(out.begin(), out.end(), [](const L& l) { return l.t < 0; }), out.end());
        std::stable_sort(out.begin(), out.end(), [](const L& a, const L& b) { return a.t < b.t; });
    }
    if (synced) *synced = any;
    QVariantList res;
    for (const L& l : out) res.push_back(QVariantMap{{"t", l.t}, {"text", l.s}});
    return res;
}

// ---- lookup ------------------------------------------------------------------------------

QString Lyrics::cacheKey() const
{
    return normalise(m_track.title) + QChar(0x1f) + normalise(m_track.artist) + QChar(0x1f) + QString::number(m_track.durationMs / 2000);
}

void Lyrics::request(const Track& t)
{
    m_track = t;
    ++m_gen;
    m_lines.clear(); m_synced = false; m_current = -1; m_source.clear(); m_matched.clear();
    m_state = QStringLiteral("loading");
    emit linesChanged(); emit currentChanged(); emit stateChanged();
    if (t.title.isEmpty()) { m_state = QStringLiteral("none"); emit stateChanged(); return; }
    m_debounce.start();
}

void Lyrics::retry()
{
    if (m_track.title.isEmpty()) return;
    Db::get().clearLyrics(cacheKey());
    ++m_gen;
    m_state = QStringLiteral("loading");
    emit stateChanged();
    run(false);
}

void Lyrics::apply(const QString& provider, const QString& body, bool synced, const QString& matched)
{
    bool isSynced = false;
    QVariantList lines = synced ? parseLrc(body, &isSynced) : QVariantList();
    if (!isSynced) {
        lines.clear();
        for (const QString& l : body.split('\n')) lines.push_back(QVariantMap{{"t", -1}, {"text", l.trimmed()}});
    }
    m_lines = lines; m_synced = isSynced; m_source = provider; m_matched = matched;
    m_state = lines.isEmpty() ? QStringLiteral("none") : QStringLiteral("found");
    m_current = -1;
    emit linesChanged(); emit currentChanged(); emit stateChanged();
    setPosition(m_posMs);
}

void Lyrics::run(bool useCache)
{
    const int gen = m_gen;
    // 1. sidecar file
    if (!m_track.path.contains("://")) {
        const QString lrc = QFileInfo(m_track.path).dir().filePath(QFileInfo(m_track.path).completeBaseName() + ".lrc");
        QFile f(lrc);
        if (f.open(QIODevice::ReadOnly)) {
            const QString body = QString::fromUtf8(f.readAll());
            bool synced = false;
            parseLrc(body, &synced);
            apply(tr("Local file"), body, synced, m_track.title);
            if (m_state == QLatin1String("found")) return;
        }
    }
    // 2. cache (a stored miss is trusted for a week)
    if (useCache) {
        if (auto c = Db::get().lyrics(cacheKey())) {
            if (!c->text.isEmpty()) { apply(c->provider, c->text, c->synced, m_track.title); return; }
            if (QDateTime::currentSecsSinceEpoch() - c->fetched < 7 * 86400) { finishNone(gen); return; }
        }
    }
    // 3. providers
    m_providers = m_plugins->enabledWith("lyrics");
    if (m_providers.isEmpty()) { finishNone(gen); return; }
    tryProvider(0, gen);
}

void Lyrics::finishNone(int gen)
{
    if (gen != m_gen) return;
    m_lines.clear(); m_synced = false;
    m_state = QStringLiteral("none");
    emit linesChanged(); emit stateChanged();
}

void Lyrics::tryProvider(int index, int gen)
{
    if (gen != m_gen) return;
    if (index >= m_providers.size()) {
        Db::get().putLyrics(cacheKey(), {QString(), false, QString(), 0});     // remember the miss
        finishNone(gen);
        return;
    }
    const QString id = m_providers[index];
    const QJsonObject params{{"title", m_track.title}, {"artist", m_track.artist}, {"album", m_track.album},
                             {"durationMs", double(m_track.durationMs)}};
    m_plugins->call(id, "lyrics.search", params, [this, id, index, gen](bool ok, const QJsonValue& v, const QString& err) {
        if (gen != m_gen) return;
        if (!ok) { tryProvider(index + 1, gen); return; }

        double best = 0;
        QJsonObject pick;
        for (const QJsonValue& cv : v.toArray()) {
            const QJsonObject c = cv.toObject();
            const QString synced = c.value("synced").toString(), plain = c.value("plain").toString();
            if (synced.isEmpty() && plain.isEmpty()) continue;
            double score = 0.70 * similarity(m_track.title, c.value("title").toString());
            const QString artist = c.value("artist").toString();
            score += 0.20 * (artist.isEmpty() || m_track.artist.isEmpty() ? 0.5 : similarity(m_track.artist, artist));
            const double dur = c.value("durationMs").toDouble();
            if (dur > 0 && m_track.durationMs > 0) {
                const double d = std::abs(dur - double(m_track.durationMs)) / 1000.0;
                score += 0.10 * (d <= 2 ? 1.0 : d <= 5 ? 0.6 : d <= 10 ? 0.3 : 0.0);
            } else score += 0.05;
            if (!synced.isEmpty()) score += 0.03;        // prefer synced on a tie
            if (score > best) { best = score; pick = c; }
        }
        if (best < 0.62 || pick.isEmpty()) { tryProvider(index + 1, gen); return; }

        const QString synced = pick.value("synced").toString();
        const bool isSynced = !synced.isEmpty();
        const QString body = isSynced ? synced : pick.value("plain").toString();
        const QString name = m_plugins->nameOf(id);
        Db::get().putLyrics(cacheKey(), {name, isSynced, body, 0});
        apply(name, body, isSynced, pick.value("title").toString());
    }, 12000);
}

void Lyrics::setPosition(qint64 ms)
{
    m_posMs = ms;
    if (!m_synced || m_lines.isEmpty()) return;
    int lo = 0, hi = int(m_lines.size()) - 1, ans = -1;
    const qint64 t = ms + 120;      // highlight a touch early, feels better
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (m_lines[mid].toMap().value("t").toLongLong() <= t) { ans = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (ans != m_current) { m_current = ans; emit currentChanged(); }
}
