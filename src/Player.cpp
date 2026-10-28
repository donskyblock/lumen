#include "Player.h"
#include "Db.h"
#include "TextUtil.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QDateTime>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>
#include <algorithm>

Player::Player(AudioEngine* engine, Library* lib, PluginManager* plugins, QObject* parent) : QObject(parent), m_engine(engine), m_lib(lib), m_plugins(plugins)
{
    QSettings s;
    m_volume = s.value("player/volume", 1.0).toDouble();
    m_shuffle = s.value("player/shuffle", false).toBool();
    m_repeat = s.value("player/repeat", 0).toInt();
    m_autoplay = s.value("player/autoplay", true).toBool();
    applyGain();
    pruneStreamCache();

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

Player::~Player() { finishListen(); }

void Player::applyGain() { m_engine->setGain(float(m_volume * m_volume)); }   // gentle perceptual curve

// Full-length remote audio is still streamed on demand. Once a catalog song
// has earned a repeat listen, fetch a copy in the background so the next play
// starts from disk. The logical catalog id, not a short-lived signed URL, is
// the cache key.
QString Player::cachedStream(const QString& logicalPath) const
{
    if (!isCatalogPath(logicalPath)) return {};
    const QByteArray id = QCryptographicHash::hash(logicalPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString file = QDir(Db::get().dataDir()).filePath("streamcache/" + QString::fromLatin1(id) + ".audio");
    const QFileInfo info(file);
    return info.isFile() && info.size() >= 16 * 1024 ? file : QString();
}

void Player::pruneStreamCache() const
{
    constexpr qint64 maxBytes = 512LL * 1024 * 1024;
    const QDir dir(QDir(Db::get().dataDir()).filePath("streamcache"));
    if (!dir.exists()) { QDir().mkpath(dir.path()); return; }
    QVector<QFileInfo> files;
    qint64 total = 0;
    QDirIterator it(dir.path(), {"*.audio"}, QDir::Files);
    while (it.hasNext()) { it.next(); const QFileInfo f = it.fileInfo(); files << f; total += f.size(); }
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() < b.lastModified(); });
    for (const QFileInfo& f : std::as_const(files)) {
        if (total <= maxBytes) break;
        if (QFile::remove(f.filePath())) total -= f.size();
    }
}

void Player::cacheStream(const Track& track, const Resolved& resolved)
{
    constexpr qint64 maxOneFile = 100LL * 1024 * 1024;
    if (!isCatalogPath(track.path) || !resolved.full || m_lib->stats()->count(track.path) < 1 || !cachedStream(track.path).isEmpty() || m_cacheDownloads.contains(track.path)) return;
    const QUrl url(resolved.url);
    if (url.scheme() != "http" && url.scheme() != "https") return;

    const QString target = QDir(Db::get().dataDir()).filePath("streamcache/" + QString::fromLatin1(QCryptographicHash::hash(track.path.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".audio");
    auto out = std::make_shared<QSaveFile>(target);
    if (!out->open(QIODevice::WriteOnly)) return;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Lumen/0.2");
    for (const QByteArray& line : resolved.headers.toUtf8().split('\n')) {
        const int colon = line.indexOf(':');
        if (colon > 0) request.setRawHeader(line.left(colon).trimmed(), line.mid(colon + 1).trimmed());
    }
    QNetworkReply* reply = m_cacheNet.get(request);
    m_cacheDownloads.insert(track.path, reply);
    auto written = std::make_shared<qint64>(0);
    connect(reply, &QNetworkReply::readyRead, this, [reply, out, written] {
        const QByteArray chunk = reply->readAll();
        *written += chunk.size();
        if (*written > maxOneFile || out->write(chunk) != chunk.size()) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, out, written, path = track.path] {
        if (reply->bytesAvailable()) {
            const QByteArray chunk = reply->readAll();
            *written += chunk.size();
            if (*written <= 100LL * 1024 * 1024) out->write(chunk);
        }
        const bool good = reply->error() == QNetworkReply::NoError && *written >= 16 * 1024 && *written <= 100LL * 1024 * 1024;
        if (good) out->commit(); else out->cancelWriting();
        m_cacheDownloads.remove(path);
        reply->deleteLater();
        if (good) pruneStreamCache();
    });
}

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
    // A new queue invalidates any autoplay request still in flight for the old one. Without this the
    // busy flag stays set, the new queue never asks for its own recommendations, and the old song's
    // suggestions land in the new Up Next when they arrive.
    ++m_autoTicket;
    m_autoBusy = false;
    m_autoAdvance = false;
    m_fixed = m_nextFixed;                 // decided before the first song starts, so it never asks autoplay for more
    m_nextFixed = false;
    ++m_upgradeSerial;
    m_upgrading = m_upgradeEnded = false;
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

void Player::playAlbum(const QString& albumKey, int startIndex, bool shuffled)
{
    QVector<Track> items = m_lib->albumTracks(albumKey);
    if (items.isEmpty()) return;
    if (shuffled && !m_shuffle) { m_shuffle = true; QSettings().setValue("player/shuffle", true); emit modeChanged(); }
    const int start = shuffled ? int(QRandomGenerator::global()->bounded(quint32(items.size()))) : startIndex;
    setQueue(std::move(items), start);
    startAt(m_pos);
}

void Player::playPathsShuffled(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    if (!m_shuffle) { m_shuffle = true; QSettings().setValue("player/shuffle", true); emit modeChanged(); }
    playPaths(paths, int(QRandomGenerator::global()->bounded(quint32(paths.size()))));
}

void Player::playNext(const QStringList& paths)
{
    QVector<Track> add;
    for (const QString& p : paths) if (auto t = m_lib->trackByPath(p)) add.push_back(*t);
    if (add.isEmpty()) return;
    if (!hasTrack()) { setQueue(add, 0); startAt(m_pos); return; }
    int at = m_pos + 1;
    for (const Track& t : add) {
        m_items.push_back(t);
        m_order.insert(at++, int(m_items.size()) - 1);
    }
    queueFollowing(m_pos);
    emit queueChanged();
}

void Player::addToQueue(const QStringList& paths)
{
    QVector<Track> add;
    for (const QString& p : paths) if (auto t = m_lib->trackByPath(p)) add.push_back(*t);
    if (add.isEmpty()) return;
    if (!hasTrack()) { setQueue(add, 0); startAt(m_pos); return; }
    for (const Track& t : add) {
        m_items.push_back(t);
        m_order.push_back(int(m_items.size()) - 1);
    }
    if (followingPos(m_pos) < 0 || m_engine->playing() == false) queueFollowing(m_pos);
    emit queueChanged();
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
    m_waitingNext = false;
    const int f = followingPos(afterPos);
    if (f < 0) { m_engine->setNext({}, -1); return; }
    const Track& nt = m_items[m_order[f]];
    QString url = nt.path, headers = nt.headers;
    if (needsResolve(nt.path)) {
        const auto r = m_resolver ? m_resolver->cached(nt.path) : std::nullopt;
        if (!r || !r->ok) {
            // Not resolved yet: look it up now so it can start gaplessly, and fall back to
            // starting it when this song ends if that's not in time.
            m_engine->setNext({}, -1);
            m_waitingNext = true;
            if (m_resolver && f != afterPos) {
                const quint64 serial = m_startSerial;
                const int cur = afterPos;
                m_resolver->resolve(nt, [this, serial, cur](const Resolved& res) {
                    if (serial != m_startSerial || !res.ok || m_pos != cur) return;
                    queueFollowing(cur);
                });
            }
            return;
        }
        url = r->url; headers = r->headers;
        m_nextRes = *r;
    }
    const qint64 token = ++m_tokenSeq;
    m_posByToken.insert(token, f);
    m_engine->setNext(url, token, headers);
}

void Player::setAutoplay(bool on)
{
    if (m_autoplay == on) return;
    m_autoplay = on;
    QSettings().setValue("player/autoplay", on);
    if (!on) { ++m_autoTicket; m_autoBusy = false; m_autoAdvance = false; }
    emit modeChanged();
    if (on) maybeRequestAutoplay();
}

// Ask for more songs when the queue is about to run dry (repeat modes keep it going on their own).
void Player::maybeRequestAutoplay()
{
    // No autoplay on a repeating queue, and none on a playlist: a playlist is exactly the songs you chose.
    if (!m_autoplay || m_autoBusy || m_repeat != 0 || m_fixed || m_order.isEmpty() || m_pos < 0) return;
    // Keep a real recommendation runway visible in Up Next.  Taste combines
    // local genre/artist matches, related catalog artists and enabled source
    // recommenders, then applies the listener's profile before we append it.
    if (int(m_order.size()) - 1 - m_pos > 8) return;
    QStringList seeds, exclude;
    for (int i = std::max(0, m_pos - 2); i <= m_pos && i < m_order.size(); ++i) seeds << m_items[m_order[i]].path;
    for (int i : std::as_const(m_order)) exclude << m_items[i].path;
    m_autoBusy = true;
    emit modeChanged();
    emit autoplayWanted(++m_autoTicket, seeds, exclude);
}

void Player::autoplayArrived(int ticket, const QStringList& paths, const QStringList&)
{
    if (ticket != m_autoTicket) return;
    m_autoBusy = false;
    emit modeChanged();
    const int before = int(m_order.size());
    if (!paths.isEmpty() && m_autoplay) {
        QVector<Track> add;
        for (const QString& p : paths) if (auto t = m_lib->trackByPath(p)) add.push_back(*t);
        for (const Track& t : add) {
            m_items.push_back(t);
            m_order.push_back(int(m_items.size()) - 1);
        }
        emit queueChanged();
    }
    if (m_autoAdvance) {
        m_autoAdvance = false;
        if (int(m_order.size()) > before) { finishListen(); startAt(before); return; }
        finishListen(); m_stopped = true; m_position = 0; setPlaying(false);
        emit trackChanged(); emit positionChanged();
        return;
    }
    if (m_order.size() > before) queueFollowing(m_pos);
}

void Player::finishListen()
{
    if (m_listenPath.isEmpty()) return;
    // counts as a play after 30 s, or half of a short song
    const qint64 need = std::min<qint64>(30000, std::max<qint64>(8000, m_listenDur / 2));
    if (m_listenMs >= need) {
        if (!isTransientRemote(m_listenPath)) m_lib->stats()->record(m_listenPath, m_listenMs);
        // the song index learns from every counted listen, online ones included: it is what "songs from
        // what you've listened to" is built on
        if (m_index) if (const auto t = m_lib->trackByPath(m_listenPath)) m_index->noteListen(*t);
    }
    m_listenPath.clear();
    m_listenMs = 0;
}

void Player::announceTrack()
{
    finishListen();
    if (const Track* t = cur()) { m_listenPath = t->path; m_listenDur = t->durationMs; m_listenMs = 0; m_tickClock.restart(); }
    if (const Track* t = cur()) {
        if (t->path.contains("://")) emit paletteReset(); else emit coverForPalette(t->path);
        emit trackInfo(*t);
    }
    // Songs from extensions have upload-style names; give the lookup a moment to find the real
    // recording first so autoplay branches from the right artist.
    if (!startEnrich()) maybeRequestAutoplay();
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
    const quint64 serial = ++m_startSerial;
    m_streamRetries = 0;
    ++m_upgradeSerial;
    m_upgrading = m_upgradeEnded = false;
    if (!m_autoSkipping) m_failSkips = 0;
    m_autoSkipping = false;
    m_nextFailedPos = -1;
    m_resumeFrom = startMs;
    m_curVia.clear();
    m_curViaName.clear();
    const Track& tr0 = m_items[m_order[orderPos]];
    if (needsResolve(tr0.path)) {
        const auto cached = m_resolver ? m_resolver->cached(tr0.path) : std::nullopt;
        if (cached && cached->ok) {
            startResolved(orderPos, token, startMs, autoplay, *cached);
        } else {
            // Nothing to hear yet: silence the previous song, show this one, and look for a source.
            m_engine->stop();
            m_waitingNext = false;
            m_resolving = true;
            emit resolvingChanged();
            setPlaying(autoplay);
            emit trackChanged();
            emit queueChanged();
            emit positionChanged();
            emit errorChanged();
            announceTrack();
            if (!m_resolver) { m_resolving = false; m_error = tr("Online playback isn't available."); emit errorChanged(); setPlaying(false); return; }
            const Track want = realTrack(tr0);
            m_resolver->resolve(want, [this, serial, orderPos, token, startMs, autoplay](const Resolved& r) {
                if (serial != m_startSerial) return;          // user moved on
                m_resolving = false;
                emit resolvingChanged();
                if (!r.ok) {
                    m_error = r.error.isEmpty() ? tr("Couldn't find anything to play for this song.") : r.error;
                    m_stopped = true;
                    setPlaying(false);
                    emit errorChanged();
                    return;
                }
                startResolved(orderPos, token, startMs, autoplay && m_playing, r);
            });
            saveSession();
            return;
        }
        saveSession();
        return;
    }
    m_engine->load(tr0.path, token, startMs, autoplay, tr0.headers);
    m_waitingNext = false;
    queueFollowing(orderPos);
    setPlaying(autoplay);
    emit trackChanged();
    emit queueChanged();
    emit positionChanged();
    emit errorChanged();
    announceTrack();
    saveSession();
}

// A catalog song now has an address: describe what we are really playing and start it.
void Player::startResolved(int orderPos, qint64 token, qint64 startMs, bool autoplay, const Resolved& r)
{
    if (orderPos < 0 || orderPos >= m_order.size()) return;
    Track& t = m_items[m_order[orderPos]];
    m_currentResolved = r;
    m_curVia = r.full ? QStringLiteral("full") : QStringLiteral("preview");
    const QString disk = cachedStream(t.path);
    m_curViaName = disk.isEmpty() ? r.via : tr("Local cache");
    t.headers = r.headers;
    t.codec = r.full ? (r.format.isEmpty() ? QStringLiteral("Stream") : r.format) : tr("Preview");
    t.lossless = r.lossless;
    t.sampleRate = r.sampleRate;
    t.bitDepth = r.bitDepth;
    t.bitrateKbps = r.bitrateKbps;
    if (r.durationMs > 0) t.durationMs = r.durationMs;
    m_listenDur = t.durationMs;
    m_resolving = false;
    m_error.clear();
    emit resolvingChanged();
    if (r.durationMs > 0 && startMs >= r.durationMs - 500) startMs = 0;      // saved position is past the end of a preview
    m_position = startMs;
    m_resumeFrom = startMs;
    m_engine->load(disk.isEmpty() ? r.url : disk, token, startMs, autoplay, disk.isEmpty() ? r.headers : QString());
    if (disk.isEmpty()) cacheStream(t, r);
    queueFollowing(orderPos);
    setPlaying(autoplay);
    emit trackChanged();
    emit queueChanged();
    emit positionChanged();
    emit errorChanged();
    if (m_listenPath != t.path) announceTrack();
    else if (autoplay) m_tickClock.restart();
    saveSession();
    if (!r.full) maybeUpgrade(orderPos, token, t.path);
}

Track Player::realTrack(const Track& t) const
{
    Track out = t;
    if (isCatalogPath(t.path))
        if (const auto orig = m_lib->trackByPath(t.path); orig && orig->durationMs > 0) out.durationMs = orig->durationMs;
    return out;
}

// The 30 s preview is playing although the song is longer. Start it right away (nothing waits), and meanwhile
// search the source extension (YouTube...) for the real thing. If it is found, switch to it at the same point.
void Player::maybeUpgrade(int orderPos, qint64 token, const QString& path)
{
    if (!isCatalogPath(path) || !m_resolver || m_plugins->enabledWith("source").isEmpty()) return;
    const Track real = realTrack(m_items[m_order[orderPos]]);
    if (real.durationMs <= 35000) return;                  // a genuinely short song: the preview is the whole thing
    const quint64 serial = ++m_upgradeSerial;
    m_upgrading = true;
    m_upgradeEnded = false;
    m_engine->setNext({}, -1);                             // don't glide into the next song before we have looked
    m_waitingNext = false;
    emit notice(tr("Only a 30-second preview was available. Attempting to find the full version…"));
    QTimer::singleShot(25000, this, [this, serial] { if (serial == m_upgradeSerial && m_upgrading) { ++m_upgradeSerial; failUpgrade(); } });
    m_resolver->upgrade(real, [this, serial, orderPos, token](const Resolved& r) {
        if (serial != m_upgradeSerial) return;             // moved on to another song, or timed out
        if (!r.ok || !r.full) { ++m_upgradeSerial; failUpgrade(r.error); return; }
        m_upgrading = false;
        m_upgradeEnded = false;
        if (token != m_audibleToken || orderPos < 0 || orderPos >= m_order.size()) return;
        emit notice(tr("Found the full version, switching to it."));
        const qint64 at = r.durationMs > 1000 ? std::min<qint64>(m_position, r.durationMs - 1000) : m_position;
        startResolved(orderPos, token, at, m_playing, r);
    });
}

void Player::failUpgrade(const QString& why)
{
    const bool ended = m_upgradeEnded;
    m_upgrading = false;
    m_upgradeEnded = false;
    QString reason = why.simplified();
    if (reason.size() > 160) reason = reason.left(159) + QStringLiteral("…");
    emit notice(reason.isEmpty() ? tr("Couldn't find the full version, staying on the preview.")
                                 : tr("Couldn't get the full version (%1). Staying on the preview.").arg(reason));
    if (!ended) { queueFollowing(m_pos); return; }        // preview still playing: line the next song up again
    // the preview already ran out while we were looking: carry on the way the end of a song normally does
    const int f = followingPos(m_pos);
    finishListen();
    if (f >= 0 && f != m_pos) { startAt(f, 0, m_playing); return; }
    m_stopped = true;
    m_position = 0;
    setPlaying(false);
    emit trackChanged();
    emit positionChanged();
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

// A song picked from an extension's own results. It is queued under a stable "lumen://ext/..." address
// (not a one-off stream URL), so it behaves like any other song: autoplay can seed from it, it can be
// re-resolved if its stream dies, and a copy in your own library plays instead when you have one.
void Player::playRemote(const QString& pluginId, const QVariantMap& item)
{
    const QString itemId = item.value("id").toString();
    if (itemId.isEmpty()) return;
    const UploadMeta um = parseUploadMeta(item.value("title").toString(), item.value("artist").toString());
    Track t;
    t.path = extPath(pluginId, itemId);
    t.title = um.title.isEmpty() ? item.value("title").toString() : um.title;
    t.artist = um.artist.isEmpty() ? item.value("artist").toString() : um.artist;
    t.album = item.value("album").toString();
    t.durationMs = item.value("durationMs").toLongLong();
    t.coverUrl = item.value("cover").toString();
    t.codec = QStringLiteral("Stream");
    t.added = QDateTime::currentSecsSinceEpoch();
    m_lib->noteRemote({t});
    setQueue({t}, 0);
    startAt(m_pos);
}

void Player::playFixed(const QStringList& paths, int start)
{
    m_nextFixed = true;
    playPaths(paths, start);
}

void Player::playOnlineSong(const QString& path, const QStringList& context)
{
    const auto t = m_lib->trackByPath(path);
    if (!t) return;
    if (!m_autoplay) {
        if (context.isEmpty()) { playPaths({path}, 0); return; }
        playPaths(context, std::max<int>(0, int(context.indexOf(path))));
        return;
    }
    setQueue({*t}, 0);
    startAt(m_pos);
}

// ---- proper names for extension songs ------------------------------------------------------------

bool Player::startEnrich()
{
    const Track* t = cur();
    if (!t || !m_meta || !isExtPath(t->path) || m_enriched.contains(t->path)) return false;
    m_enriched.insert(t->path);
    if (m_enriched.size() > 500) m_enriched.clear();
    const QString path = t->path;
    m_enrichPending = path;
    m_meta->lookup(t->title, t->artist, t->durationMs, [this, path](const SongMeta& m) {
        applyMeta(path, m);
        finishEnrich(path);
    });
    QTimer::singleShot(6000, this, [this, path] { finishEnrich(path); });      // never hold autoplay hostage to a slow provider
    return true;
}

void Player::finishEnrich(const QString& path)
{
    if (m_enrichPending != path) return;
    m_enrichPending.clear();
    if (cur() && cur()->path == path) maybeRequestAutoplay();
}

void Player::applyMeta(const QString& path, const SongMeta& m)
{
    if (!m.ok) return;
    bool touched = false;
    Track updated;
    for (Track& t : m_items) {
        if (t.path != path) continue;
        if (!m.title.isEmpty()) t.title = m.title;
        if (!m.artist.isEmpty()) t.artist = m.artist;
        if (!m.album.isEmpty()) t.album = m.album;
        if (!m.genre.isEmpty()) t.genre = m.genre;
        if (m.year > 0) t.year = m.year;
        if (!m.artistId.isEmpty()) t.artistId = m.artistId;
        if (!m.albumId.isEmpty()) t.albumId = m.albumId;
        if (!m.cover.isEmpty()) t.coverUrl = m.cover;          // album art beats a video thumbnail
        updated = t;
        touched = true;
    }
    if (!touched) return;
    m_lib->updateRemote(updated);
    if (cur() && cur()->path == path) {
        emit trackChanged();
        emit queueChanged();
        emit trackInfo(*cur());                               // lyrics look-up gets the real title and artist too
    }
}

// ---- session persistence ------------------------------------------------------------

void Player::saveSession()
{
    Db& db = Db::get();
    if (m_order.isEmpty() || !cur() || isTransientRemote(cur()->path)) { if (m_order.isEmpty()) db.setKv("session", {}); return; }
    QJsonArray paths;
    int pos = 0;
    for (int i = 0; i < m_order.size() && paths.size() < 2000; ++i) {
        const Track& t = m_items[m_order[i]];
        if (isTransientRemote(t.path)) continue;
        if (i == m_pos) pos = paths.size();
        paths << t.path;
    }
    db.setKv("session", QString::fromUtf8(QJsonDocument(QJsonObject{{"paths", paths}, {"pos", pos}, {"ms", double(m_position)}, {"fixed", m_fixed}})
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
    m_fixed = o.value("fixed").toBool();
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

void Player::noteSkipIfEarly()
{
    if (m_listenPath.isEmpty() || m_listenDur <= 0 || isTransientRemote(m_listenPath)) return;
    if (m_listenMs > 1500 && m_listenMs < m_listenDur / 4) {
        Db::get().recordSkip(m_listenPath, QDateTime::currentSecsSinceEpoch());
        emit skipped(m_listenPath);
    }
}

void Player::next()
{
    if (!hasTrack()) return;
    noteSkipIfEarly();
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
    emit seeked();
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
    if (target >= 0 && target < m_order.size()) { noteSkipIfEarly(); startAt(target); }
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

// Something that was playing (or queued) stopped working: a link that expired, a connection that
// dropped mid-song, a file that cannot be opened. Streams are looked up again (a different upload if
// that one keeps failing) and resumed where they broke; anything that still won't play is skipped
// so one bad song can never stall the whole queue.
bool Player::handleFailure(qint64 token)
{
    const auto it = m_posByToken.constFind(token);
    if (it == m_posByToken.constEnd()) return false;          // a track we already moved on from
    const int pos = it.value();
    if (token == m_audibleToken) {
        recoverTrack(pos, m_currentResolved, std::max<qint64>(0, m_position));
        return true;
    }
    m_nextFailedPos = pos;                                    // the gapless-queued next song failed: handled when this one ends
    return false;
}

void Player::recoverTrack(int orderPos, const Resolved& used, qint64 resumeMs)
{
    if (orderPos < 0 || orderPos >= m_order.size()) return;
    const Track wanted = realTrack(m_items[m_order[orderPos]]);
    if (!needsResolve(wanted.path) || !m_resolver || m_streamRetries >= 3) {
        giveUp(orderPos, m_error);
        return;
    }
    ++m_streamRetries;
    m_engine->stop();
    m_resolving = true;
    emit resolvingChanged();
    const quint64 serial = ++m_startSerial;
    const qint64 token = ++m_tokenSeq;
    m_audibleToken = token;
    m_posByToken.clear();
    m_posByToken.insert(token, orderPos);
    m_nextFailedPos = -1;
    m_resumeFrom = resumeMs;
    m_resolver->reportFailed(wanted.path, used);
    m_resolver->resolve(wanted, [this, serial, orderPos, token, resumeMs](const Resolved& r) {
        if (serial != m_startSerial) return;
        if (!r.ok) {
            giveUp(orderPos, r.error.isEmpty() ? tr("Couldn't play this song.") : r.error);
            return;
        }
        startResolved(orderPos, token, resumeMs, m_playing, r);
    });
}

void Player::giveUp(int orderPos, const QString& why)
{
    m_resolving = false;
    emit resolvingChanged();
    const QString name = (orderPos >= 0 && orderPos < m_order.size()) ? m_items[m_order[orderPos]].title : QString();
    m_error = why.isEmpty() ? tr("Couldn't play this song.") : why;
    emit errorChanged();
    int f = -1;
    if (orderPos + 1 < m_order.size()) f = orderPos + 1;
    else if (m_repeat == 1 && m_order.size() > 1) f = 0;
    if (f >= 0 && f != orderPos && m_failSkips < 5 && m_playing) {
        ++m_failSkips;
        m_autoSkipping = true;
        emit problem(tr("Couldn't play “%1”, so it was skipped.").arg(name));
        finishListen();
        startAt(f, 0, true);
        return;
    }
    emit problem(tr("Couldn't play “%1”.").arg(name));
    m_engine->stop();
    m_stopped = true;
    setPlaying(false);
}

void Player::tick()
{
    if (m_tickClock.isValid()) { const qint64 dt = m_tickClock.restart(); if (m_playing && !m_resolving && dt < 1500) m_listenMs += dt; }
    const AudioEngine::Poll p = m_engine->poll();
    if (!p.error.isEmpty() && p.error != m_error) {
        m_error = p.error;
        emit errorChanged();
    }
    if (p.failedToken >= 0 && handleFailure(p.failedToken)) return;

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
    if (m_playing && p.token >= 0) {
        // after a few seconds of real playback past the (re)start point the stream counts as healthy again
        if (m_streamRetries > 0 && m_position - m_resumeFrom > 10000) m_streamRetries = 0;
        if (m_failSkips > 0 && m_position - m_resumeFrom > 3000) m_failSkips = 0;
    }

    if (p.ended && m_resolving) return;                   // stale report from the song we just replaced
    if (p.ended && m_upgrading) { m_upgradeEnded = true; return; }      // preview over, full version still being looked for
    if (p.ended && m_nextFailedPos >= 0 && m_nextFailedPos < m_order.size() && m_nextFailedPos != m_pos) {
        // The song queued after this one could not be opened. Give it one fresh attempt (a new stream
        // address); if that fails too it is skipped, and the queue carries on after it.
        const int pos = m_nextFailedPos;
        m_nextFailedPos = -1;
        const Track& nt = m_items[m_order[pos]];
        if (needsResolve(nt.path) && m_resolver) m_resolver->reportFailed(nt.path, m_nextRes);
        finishListen();
        startAt(pos, 0, m_playing);
        return;
    }
    if (p.ended && m_waitingNext) {                       // next catalog song wasn't resolved in time: start it now
        const int f = followingPos(m_pos);
        m_waitingNext = false;
        if (f >= 0) { finishListen(); startAt(f); return; }
    }
    if (p.ended && m_autoplay && m_repeat == 0 && !m_fixed && followingPos(m_pos) < 0 && !m_autoAdvance) {
        m_autoAdvance = true;                              // queue ended: carry on with whatever autoplay brings
        maybeRequestAutoplay();
        if (!m_autoBusy) { m_autoAdvance = false; }        // nothing to ask for -> fall through and stop
        else return;
    }
    if (p.ended && m_autoAdvance) return;                  // waiting for suggestions
    if (p.ended) {
        finishListen();
        m_stopped = true;
        m_position = 0;
        m_pos = (m_repeat == 0 && !m_order.isEmpty()) ? 0 : m_pos;
        setPlaying(false);
        emit trackChanged();
        emit positionChanged();
    }
}
