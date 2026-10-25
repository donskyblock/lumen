#include "Player.h"
#include "Db.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <algorithm>

Player::Player(AudioEngine* engine, Library* lib, PluginManager* plugins, QObject* parent) : QObject(parent), m_engine(engine), m_lib(lib), m_plugins(plugins)
{
    QSettings s;
    m_volume = s.value("player/volume", 1.0).toDouble();
    m_shuffle = s.value("player/shuffle", false).toBool();
    m_repeat = s.value("player/repeat", 0).toInt();
    applyGain();

    m_timer.setInterval(100);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Player::tick);

    m_save.setInterval(15000);
    connect(&m_save, &QTimer::timeout, this, &Player::saveSession);

    m_idle.setSingleShot(true);
    m_idle.setInterval(8000);
    connect(&m_idle, &QTimer::timeout, m_engine, &AudioEngine::suspendIfPaused);

    connect(m_engine, &AudioEngine::nextConsumed, this, &Player::onNextConsumed, Qt::QueuedConnection);
}

void Player::applyGain() { m_engine->setGain(float(m_volume * m_volume)); }   // gentle perceptual curve

void Player::setVolume(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(v, m_volume)) return;
    m_volume = v;
    applyGain();
    QSettings().setValue("player/volume", v);
    emit volumeChanged();
}

// ---- queue building ------------------------------------------------------------

void Player::setQueue(QVector<Track> items, int start)
{
    m_items = std::move(items);
    buildOrder(std::clamp(start, 0, int(m_items.size()) - 1));
}

void Player::buildOrder(int firstItem)
{
    m_order.resize(m_items.size());
    for (int i = 0; i < m_order.size(); ++i) m_order[i] = i;
    if (m_shuffle) {
        std::shuffle(m_order.begin(), m_order.end(), *QRandomGenerator::global());
        auto it = std::find(m_order.begin(), m_order.end(), firstItem);
        std::iter_swap(m_order.begin(), it);
        m_pos = 0;
    } else {
        m_pos = firstItem;
    }
}

void Player::playSong(int row)
{
    QVector<Track> items = m_lib->songsInView();
    if (row < 0 || row >= items.size()) return;
    setQueue(std::move(items), row);
    startAt(m_pos);
}

void Player::playAlbum(int row, int startIndex, bool shuffled)
{
    QVector<Track> items = m_lib->albumTracks(row);
    if (items.isEmpty()) return;
    if (shuffled && !m_shuffle) { m_shuffle = true; QSettings().setValue("player/shuffle", true); emit modeChanged(); }
    const int start = shuffled ? int(QRandomGenerator::global()->bounded(quint32(items.size()))) : startIndex;
    setQueue(std::move(items), start);
    startAt(m_pos);
}

int Player::followingPos(int pos) const
{
    if (m_order.isEmpty()) return -1;
    if (m_repeat == 2) return pos;
    if (pos + 1 < m_order.size()) return pos + 1;
    return m_repeat == 1 ? 0 : -1;
}

void Player::queueFollowing(int afterPos)
{
    const int f = followingPos(afterPos);
    if (f < 0) { m_engine->setNext({}, -1); return; }
    const qint64 token = ++m_tokenSeq;
    m_posByToken.insert(token, f);
    m_engine->setNext(m_items[m_order[f]].path, token, m_items[m_order[f]].headers);
}

void Player::announceTrack()
{
    if (const Track* t = cur()) {
        if (t->path.contains("://")) emit paletteReset(); else emit coverForPalette(t->path);
        emit trackInfo(*t);
    }
}

void Player::startAt(int orderPos, qint64 startMs, bool autoplay)
{
    if (orderPos < 0 || orderPos >= m_order.size()) return;
    m_pos = orderPos;
    m_posByToken.clear();
    const qint64 token = ++m_tokenSeq;
    m_audibleToken = token;
    m_posByToken.insert(token, orderPos);
    m_position = startMs;
    m_error.clear();
    m_stopped = false;
    m_idle.stop();
    m_engine->load(m_items[m_order[orderPos]].path, token, startMs, autoplay, m_items[m_order[orderPos]].headers);
    queueFollowing(orderPos);
    setPlaying(autoplay);
    emit trackChanged();
    emit queueChanged();
    emit positionChanged();
    emit errorChanged();
    announceTrack();
    saveSession();
}

void Player::playPaths(const QStringList& paths, int start)
{
    QVector<Track> items;
    int startIdx = 0;
    for (int i = 0; i < paths.size(); ++i) {
        if (auto t = m_lib->trackByPath(paths[i])) {
            if (i == start) startIdx = items.size();
            items.push_back(*t);
        }
    }
    if (items.isEmpty()) return;
    setQueue(std::move(items), startIdx);
    startAt(m_pos);
}

void Player::playRemote(const QString& pluginId, const QVariantMap& item)
{
    m_resolving = true;
    emit resolvingChanged();
    m_plugins->call(pluginId, "source.resolve", {{"id", item.value("id").toString()}},
        [this, item](bool ok, const QJsonValue& v, const QString& err) {
            m_resolving = false;
            emit resolvingChanged();
            const QJsonObject o = v.toObject();
            const QString url = o.value("url").toString();
            if (!ok || url.isEmpty()) {
                m_error = ok ? tr("The extension didn't return a playable address.") : err;
                emit errorChanged();
                return;
            }
            Track t;
            t.path = url;
            t.title = item.value("title").toString();
            t.artist = item.value("artist").toString();
            t.album = item.value("album").toString();
            t.durationMs = item.value("durationMs").toLongLong();
            t.coverUrl = item.value("cover").toString();
            const QJsonObject hdr = o.value("headers").toObject();
            for (auto it = hdr.begin(); it != hdr.end(); ++it)
                t.headers += it.key() + QStringLiteral(": ") + it.value().toString() + QStringLiteral("\r\n");
            t.codec = o.value("format").toString().toUpper();
            t.lossless = o.value("lossless").toBool();
            t.sampleRate = o.value("sampleRate").toInt();
            t.bitDepth = o.value("bitDepth").toInt();
            t.bitrateKbps = o.value("bitrateKbps").toInt();
            setQueue({t}, 0);
            startAt(m_pos);
        }, 25000);
}

// ---- session persistence ------------------------------------------------------------

void Player::saveSession()
{
    Db& db = Db::get();
    if (m_order.isEmpty() || !cur() || cur()->path.contains("://")) { if (m_order.isEmpty()) db.setKv("session", {}); return; }
    QJsonArray paths;
    int pos = 0;
    for (int i = 0; i < m_order.size() && paths.size() < 2000; ++i) {
        const Track& t = m_items[m_order[i]];
        if (t.path.contains("://")) continue;
        if (i == m_pos) pos = paths.size();
        paths << t.path;
    }
    db.setKv("session", QString::fromUtf8(QJsonDocument(QJsonObject{{"paths", paths}, {"pos", pos}, {"ms", double(m_position)}})
                                              .toJson(QJsonDocument::Compact)));
}

void Player::restoreSession()
{
    const QJsonObject o = QJsonDocument::fromJson(Db::get().kv("session").toUtf8()).object();
    const QJsonArray paths = o.value("paths").toArray();
    if (paths.isEmpty()) return;
    QVector<Track> items;
    int pos = 0;
    const int want = o.value("pos").toInt();
    for (int i = 0; i < paths.size(); ++i) {
        if (auto t = m_lib->trackByPath(paths[i].toString())) {
            if (i == want) pos = items.size();
            items.push_back(*t);
        }
    }
    if (items.isEmpty()) return;
    m_items = std::move(items);
    m_order.resize(m_items.size());
    for (int i = 0; i < m_order.size(); ++i) m_order[i] = i;
    startAt(pos, qint64(o.value("ms").toDouble()), false);      // paused, exactly where you left off
}

void Player::onNextConsumed(qint64 token)
{
    // The decoder has begun the queued track; line up the one after it.
    auto it = m_posByToken.constFind(token);
    if (it != m_posByToken.constEnd()) queueFollowing(it.value());
}

// ---- transport -----------------------------------------------------------------

void Player::setPlaying(bool p)
{
    if (m_playing == p) return;
    m_playing = p;
    if (p) { m_timer.start(); m_idle.stop(); m_save.start(); }
    else { m_timer.stop(); m_idle.start(); m_save.stop(); }
    emit playingChanged();
}

void Player::toggle()
{
    if (!hasTrack()) return;
    if (m_stopped) { startAt(m_pos); return; }
    m_engine->setPlaying(!m_playing);
    setPlaying(!m_playing);
    saveSession();
}

void Player::next()
{
    if (!hasTrack()) return;
    int f = m_pos + 1;
    if (f >= m_order.size()) { if (m_repeat == 0) return; f = 0; }
    startAt(f);
}

void Player::previous()
{
    if (!hasTrack()) return;
    if (m_position > 3000 || m_pos == 0) { seek(0); return; }
    startAt(m_pos - 1);
}

void Player::seek(qint64 ms)
{
    if (!hasTrack()) return;
    ms = std::clamp<qint64>(ms, 0, std::max<qint64>(0, duration() - 50));
    if (m_stopped) { startAt(m_pos, ms); return; }
    m_engine->seek(ms);
    m_position = ms;
    emit positionChanged();
}

void Player::toggleShuffle()
{
    m_shuffle = !m_shuffle;
    QSettings().setValue("player/shuffle", m_shuffle);
    if (hasTrack()) {
        const int item = m_order[m_pos];
        buildOrder(item);
        queueFollowing(m_pos);
        emit queueChanged();
    }
    emit modeChanged();
}

void Player::cycleRepeat()
{
    m_repeat = (m_repeat + 1) % 3;
    QSettings().setValue("player/repeat", m_repeat);
    if (hasTrack()) queueFollowing(m_pos);
    emit modeChanged();
    emit queueChanged();
}

void Player::jumpTo(int upNextIndex)
{
    const int target = m_pos + 1 + upNextIndex;
    if (target >= 0 && target < m_order.size()) startAt(target);
}

QVariantList Player::upNext() const
{
    QVariantList out;
    for (int p = m_pos + 1; p < m_order.size() && out.size() < 40; ++p) {
        const Track& t = m_items[m_order[p]];
        out.push_back(QVariantMap{{"title", t.title}, {"artist", t.artist}, {"cover", t.coverKey},
                                  {"durationMs", t.durationMs}});
    }
    return out;
}

void Player::tick()
{
    const AudioEngine::Poll p = m_engine->poll();
    if (!p.error.isEmpty() && p.error != m_error) { m_error = p.error; emit errorChanged(); }

    if (p.token >= 0 && p.token != m_audibleToken) {      // gapless hand-over reached our ears
        auto it = m_posByToken.constFind(p.token);
        m_audibleToken = p.token;
        if (it != m_posByToken.constEnd()) {
            m_pos = it.value();
            emit trackChanged();
            emit queueChanged();
            announceTrack();
        }
    }
    if (p.token >= 0 && p.posMs != m_position) { m_position = p.posMs; emit positionChanged(); }

    if (p.ended) {
        m_stopped = true;
        m_position = 0;
        m_pos = (m_repeat == 0 && !m_order.isEmpty()) ? 0 : m_pos;
        setPlaying(false);
        emit trackChanged();
        emit positionChanged();
    }
}
