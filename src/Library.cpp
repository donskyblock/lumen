#include "Library.h"
#include "Db.h"
#include "Probe.h"
#include "TextUtil.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

// ---- TrackModel -------------------------------------------------------------
static bool isHiRes(const Track& t) { return t.lossless && (t.sampleRate > 48000 || t.bitDepth > 16); }

QVariant TrackModel::data(const QModelIndex& i, int role) const
{
    if (!i.isValid() || i.row() >= m_tracks.size()) return {};
    const Track& t = m_tracks[i.row()];
    switch (role) {
    case TitleRole:    return t.title;
    case ArtistRole:   return t.artist;
    case AlbumRole:    return t.album;
    case DurationRole: return t.durationMs;
    case QualityRole:  return t.quality();
    case LosslessRole: return t.lossless;
    case TrackNoRole:  return t.trackNo;
    case CoverRole:    return t.coverKey;
    case PathRole:     return t.path;
    case GenreRole:    return t.genre;
    case YearRole:     return t.year;
    case AddedRole:    return t.added;
    case PlaysRole:    return m_stats ? m_stats->count(t.path) : 0;
    case HiResRole:    return isHiRes(t);
    case CodecRole:    return t.codec;
    }
    return {};
}

QHash<int, QByteArray> TrackModel::roleNames() const
{
    return {{TitleRole, "title"}, {ArtistRole, "artist"}, {AlbumRole, "album"}, {DurationRole, "durationMs"},
            {QualityRole, "quality"}, {LosslessRole, "lossless"}, {TrackNoRole, "trackNo"}, {CoverRole, "cover"},
            {PathRole, "path"}, {GenreRole, "genre"}, {YearRole, "year"}, {AddedRole, "added"}, {PlaysRole, "plays"},
            {HiResRole, "hires"}, {CodecRole, "codec"}};
}

void TrackModel::reset(QVector<Track> t)
{
    beginResetModel();
    m_tracks = std::move(t);
    endResetModel();
}

void TrackModel::statsChanged()
{
    if (m_tracks.isEmpty()) return;
    emit dataChanged(index(0), index(m_tracks.size() - 1), {PlaysRole});
}

// ---- AlbumModel -------------------------------------------------------------
QVariant AlbumModel::data(const QModelIndex& i, int role) const
{
    if (!i.isValid() || i.row() >= m_albums.size()) return {};
    const Album& a = m_albums[i.row()];
    switch (role) {
    case KeyRole:      return a.key;
    case TitleRole:    return a.title;
    case ArtistRole:   return a.artist;
    case YearRole:     return a.year;
    case CountRole:    return a.tracks.size();
    case CoverRole:    return a.coverKey;
    case LosslessRole: return a.lossless;
    case AddedRole:    return a.added;
    case HiResRole:    return a.hires;
    case CodecRole:    return a.codecs.join(',');
    case PlaysRole: {
        int n = 0;
        if (m_stats && m_tm) for (int t : a.tracks) n += m_stats->count(m_tm->tracks().at(t).path);
        return n;
    }
    }
    return {};
}

QHash<int, QByteArray> AlbumModel::roleNames() const
{
    return {{KeyRole, "key"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {YearRole, "year"}, {CountRole, "count"},
            {CoverRole, "cover"}, {LosslessRole, "lossless"}, {AddedRole, "added"}, {PlaysRole, "plays"},
            {HiResRole, "hires"}, {CodecRole, "codec"}};
}

void AlbumModel::reset(QVector<Album> a) { beginResetModel(); m_albums = std::move(a); endResetModel(); }
void AlbumModel::statsChanged() { if (!m_albums.isEmpty()) emit dataChanged(index(0), index(m_albums.size() - 1), {PlaysRole}); }

// ---- ArtistModel ------------------------------------------------------------
QVariant ArtistModel::data(const QModelIndex& i, int role) const
{
    if (!i.isValid() || i.row() >= m_artists.size()) return {};
    const Artist& a = m_artists[i.row()];
    switch (role) {
    case NameRole:       return a.name;
    case AlbumCountRole: return a.albums.size();
    case CountRole:      return a.tracks.size();
    case CoverRole:      return a.coverKey;
    case PlaysRole: {
        int n = 0;
        if (m_stats && m_tm) for (int t : a.tracks) n += m_stats->count(m_tm->tracks().at(t).path);
        return n;
    }
    }
    return {};
}

QHash<int, QByteArray> ArtistModel::roleNames() const
{
    return {{NameRole, "name"}, {AlbumCountRole, "albumCount"}, {CountRole, "count"}, {CoverRole, "cover"}, {PlaysRole, "plays"}};
}

void ArtistModel::reset(QVector<Artist> a) { beginResetModel(); m_artists = std::move(a); endResetModel(); }
void ArtistModel::statsChanged() { if (!m_artists.isEmpty()) emit dataChanged(index(0), index(m_artists.size() - 1), {PlaysRole}); }

// ---- ViewProxy --------------------------------------------------------------
ViewProxy::ViewProxy(QObject* parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &ViewProxy::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ViewProxy::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ViewProxy::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ViewProxy::countChanged);
}

int ViewProxy::roleId(const char* name) const
{
    if (!sourceModel()) return -1;
    const auto names = sourceModel()->roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) if (it.value() == name) return it.key();
    return -1;
}

void ViewProxy::resort()
{
    const int role = m_key == QLatin1String("default") ? -1 : roleId(m_key.toUtf8().constData());
    if (role < 0) { sort(-1); return; }
    setSortRole(role);
    sort(0, m_desc ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void ViewProxy::setSortKey(const QString& k) { if (k == m_key) return; m_key = k; resort(); emit viewChanged(); }
void ViewProxy::setDescending(bool d) { if (d == m_desc) return; m_desc = d; resort(); emit viewChanged(); }
void ViewProxy::setLosslessOnly(bool b) { if (b == m_lossless) return; m_lossless = b; invalidateFilter(); emit viewChanged(); emit countChanged(); }
void ViewProxy::setHiResOnly(bool b) { if (b == m_hires) return; m_hires = b; invalidateFilter(); emit viewChanged(); emit countChanged(); }
void ViewProxy::setCodec(const QString& c) { if (c == m_codec) return; m_codec = c; invalidateFilter(); emit viewChanged(); emit countChanged(); }

bool ViewProxy::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (!m_lossless && !m_hires && m_codec.isEmpty()) return true;
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    if (m_lossless) { const int r = roleId("lossless"); if (r >= 0 && !sourceModel()->data(idx, r).toBool()) return false; }
    if (m_hires) { const int r = roleId("hires"); if (r >= 0 && !sourceModel()->data(idx, r).toBool()) return false; }
    if (!m_codec.isEmpty()) {
        const int r = roleId("codec");
        if (r >= 0 && !sourceModel()->data(idx, r).toString().split(',').contains(m_codec)) return false;
    }
    return true;
}

bool ViewProxy::lessThan(const QModelIndex& l, const QModelIndex& r) const
{
    const QVariant a = sourceModel()->data(l, sortRole()), b = sourceModel()->data(r, sortRole());
    int c = 0;
    if (a.typeId() == QMetaType::QString) c = QString::compare(foldText(a.toString()), foldText(b.toString()));
    else { const double x = a.toDouble(), y = b.toDouble(); c = x < y ? -1 : (x > y ? 1 : 0); }
    if (c != 0) return c < 0;
    return l.row() < r.row();           // stable: fall back to the library's own order
}

// ---- Scanner ----------------------------------------------------------------
void Scanner::run(QStringList folders, Signatures known)
{
    cancel = false;
    QVector<Track> buf;
    QSet<QString> seen;
    int examined = 0;
    for (const QString& folder : folders) {
        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext() && !cancel) {
            const QString path = it.next();
            if (!isAudioFile(QFileInfo(path).suffix().toLower())) continue;
            seen.insert(path);
            ++examined;
            const QFileInfo fi(path);
            const auto sig = known.constFind(path);
            if (sig == known.constEnd() || sig->first != fi.lastModified().toSecsSinceEpoch() || sig->second != fi.size()) {
                if (auto t = probeFile(path)) buf.push_back(*t);
            }
            if (buf.size() >= 64) { emit batch(buf); buf.clear(); }
            if (examined % 200 == 0) emit progress(examined);
        }
    }
    if (!buf.isEmpty()) emit batch(buf);
    emit progress(examined);

    QStringList removed;
    if (!cancel) {
        for (auto it = known.cbegin(); it != known.cend(); ++it) {
            if (seen.contains(it.key())) continue;
            for (const QString& f : folders)
                if (it.key().startsWith(f + QLatin1Char('/'))) { removed << it.key(); break; }
        }
    }
    emit done(removed);
}

// ---- Library ----------------------------------------------------------------
Library::Library(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<Signatures>("Signatures");
    m_tracks.setStats(&m_stats);
    m_albums.setSources(&m_tracks, &m_stats);
    m_artists.setSources(&m_tracks, &m_stats);
    m_songsView.setSourceModel(&m_tracks);
    m_albumsView.setSourceModel(&m_albums);
    m_artistsView.setSourceModel(&m_artists);
    connect(&m_stats, &PlayStats::changed, this, [this] { m_tracks.statsChanged(); m_albums.statsChanged(); m_artists.statsChanged(); });

    m_scanner = new Scanner;
    m_scanner->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_scanner, &QObject::deleteLater);
    connect(m_scanner, &Scanner::batch, this, &Library::onBatch, Qt::QueuedConnection);
    connect(m_scanner, &Scanner::progress, this, [this](int n) { m_scanned = n; emit scanProgress(); }, Qt::QueuedConnection);
    connect(m_scanner, &Scanner::done, this, &Library::onDone, Qt::QueuedConnection);
    m_thread.start(QThread::LowPriority);

    Db& db = Db::get();
    m_folders = db.folders();
    if (db.kv("library.initialised").isEmpty()) {
        db.setKv("library.initialised", "1");
        QStringList seed = QSettings().value("library/folders").toStringList();   // older builds
        if (seed.isEmpty()) {
            const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (QFileInfo(music).isDir()) seed << music;
        }
        for (const QString& f : seed) { db.addFolder(f); m_folders << f; }
    }

    // Show the cached library immediately, then refresh in the background.
    for (const Track& t : db.allTracks()) m_map.insert(t.path, t);
    for (const Track& t : db.catTracks(true)) m_map.insert(t.path, t);       // songs imported from the online catalog
    publish();
    if (!m_folders.isEmpty())
        QMetaObject::invokeMethod(this, [this] { scan(); }, Qt::QueuedConnection);
}

Library::~Library()
{
    m_scanner->cancel = true;
    m_thread.quit();
    m_thread.wait();
}

bool Library::covered(const QString& path) const
{
    for (const QString& f : m_folders)
        if (path.startsWith(f + QLatin1Char('/'))) return true;
    return false;
}

QVariantList Library::folders() const
{
    QVariantList out;
    for (const QString& f : m_folders) {
        int n = 0;
        const QString prefix = f + QLatin1Char('/');
        for (auto it = m_map.cbegin(); it != m_map.cend(); ++it)
            if (it.key().startsWith(prefix)) ++n;
        out.push_back(QVariantMap{{"path", f}, {"count", n}, {"exists", QFileInfo(f).isDir()}});
    }
    return out;
}

QString Library::addFolder(const QString& path)
{
    QString p = path.trimmed();
    if (p.startsWith("file://")) p = QUrl(p).toLocalFile();
    if (p.startsWith("~/") || p == "~") p = QDir::homePath() + p.mid(1);
    p = QDir::cleanPath(p);
    if (QFileInfo(p).isFile()) p = QFileInfo(p).absolutePath();     // a file was dropped: use its folder
    if (!QFileInfo(p).isDir()) return tr("That folder doesn't exist: %1").arg(p);
    for (const QString& f : std::as_const(m_folders)) {
        if (p == f) return tr("Already in your library.");
        if (p.startsWith(f + QLatin1Char('/'))) return tr("Already included through %1.").arg(f);
    }
    Db& db = Db::get();
    for (const QString& f : QStringList(m_folders))
        if (f.startsWith(p + QLatin1Char('/'))) { db.removeFolder(f); m_folders.removeAll(f); }
    db.addFolder(p);
    m_folders << p;
    emit foldersChanged();
    scan();
    return {};
}

void Library::removeFolder(const QString& path)
{
    if (!m_folders.contains(path)) return;
    m_folders.removeAll(path);
    Db& db = Db::get();
    db.removeFolder(path);
    QStringList gone;
    for (auto it = m_map.cbegin(); it != m_map.cend(); ++it)
        if (!isCatalogPath(it.key()) && !covered(it.key())) gone << it.key();
    db.begin();
    db.removeTracks(gone);
    db.commit();
    for (const QString& g : gone) m_map.remove(g);
    emit foldersChanged();
    publish();
}

void Library::scan()
{
    if (m_scanning) { m_rescanQueued = true; return; }
    if (m_folders.isEmpty()) return;
    m_scanning = true;
    m_rescanQueued = false;
    m_firstScan = std::none_of(m_map.cbegin(), m_map.cend(), [](const Track& t) { return !isCatalogPath(t.path); });
    m_scanned = 0;
    m_pending.clear();
    emit scanningChanged();
    emit scanProgress();
    QMetaObject::invokeMethod(m_scanner, "run", Qt::QueuedConnection,
                              Q_ARG(QStringList, m_folders), Q_ARG(Signatures, Db::get().signatures()));
}

void Library::onBatch(const QVector<Track>& t)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (Track x : t) {
        const auto old = m_map.constFind(x.path);
        // First scan of a fresh library: the file's own date is the best "added" we have.
        x.added = (old != m_map.constEnd() && old->added) ? old->added : (m_firstScan ? std::min(now, x.mtime) : now);
        m_map.insert(x.path, x);
        m_pending.push_back(x);
    }
}

void Library::onDone(const QStringList& removed)
{
    Db& db = Db::get();
    db.begin();
    db.upsertTracks(m_pending);
    db.removeTracks(removed);
    db.commit();
    for (const QString& r : removed) m_map.remove(r);
    const bool changedSomething = !m_pending.isEmpty() || !removed.isEmpty();
    m_pending.clear();
    if (changedSomething) publish();
    m_scanning = false;
    emit scanningChanged();
    emit foldersChanged();
    if (m_rescanQueued) scan();
}

static int cmp(const QString& a, const QString& b) { return QString::compare(a, b, Qt::CaseInsensitive); }

void Library::publish()
{
    QVector<Track> sorted = m_map.values().toVector();
    std::sort(sorted.begin(), sorted.end(), [](const Track& a, const Track& b) {
        if (int c = cmp(a.albumArtist, b.albumArtist)) return c < 0;
        if (int c = cmp(a.album, b.album)) return c < 0;
        if (a.discNo != b.discNo) return a.discNo < b.discNo;
        if (a.trackNo != b.trackNo) return a.trackNo < b.trackNo;
        return cmp(a.title, b.title) < 0;
    });
    QSet<QString> codecs;
    for (Track& t : sorted) {
        t.folded = foldText(t.title) + QChar(0x1f) + foldText(t.artist) + QChar(0x1f) + foldText(t.album) + QChar(0x1f) + foldText(t.genre);
        codecs.insert(t.codec);
        m_map[t.path].folded = t.folded;
    }
    m_localKey.clear();
    for (const Track& t : std::as_const(sorted)) {
        if (isCatalogPath(t.path)) continue;
        const QString k = normTitleKey(t.title) + QChar(0x1f) + primaryArtistKey(t.artist);
        auto it = m_localKey.find(k);
        if (it == m_localKey.end()) m_localKey.insert(k, t.path);
        else if (t.lossless && !m_map.value(it.value()).lossless) *it = t.path;
    }
    m_codecs = QStringList(codecs.begin(), codecs.end());
    m_codecs.sort();
    m_tracks.reset(std::move(sorted));
    rebuildAlbumsAndArtists();
    ++m_rev;
    emit changed();
}

void Library::rebuildAlbumsAndArtists()
{
    const auto& tracks = m_tracks.tracks();
    // albums
    QVector<Album> albums;
    m_albumByKey.clear();
    m_trackAlbum = QVector<int>(tracks.size(), -1);
    for (int i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        const QString key = foldText(t.albumArtist) + QChar(0x1f) + foldText(t.album);
        auto it = m_albumByKey.find(key);
        if (it == m_albumByKey.end()) {
            Album a;
            a.key = key; a.title = t.album; a.artist = t.albumArtist; a.coverKey = t.coverKey; a.year = t.year;
            a.folded = foldText(t.album) + QChar(0x1f) + foldText(t.albumArtist);
            it = m_albumByKey.insert(key, albums.size());
            albums.push_back(a);
        }
        Album& a = albums[it.value()];
        m_trackAlbum[i] = it.value();
        a.tracks.push_back(i);
        a.durationMs += t.durationMs;
        a.lossless = a.lossless && t.lossless;
        a.hires = a.hires || isHiRes(t);
        a.added = std::max(a.added, t.added);
        if (!a.year) a.year = t.year;
        if (!a.codecs.contains(t.codec)) a.codecs << t.codec;
    }

    // artists
    QHash<QString, Artist> byKey;
    QStringList order;
    auto artistFor = [&](const QString& name) -> Artist* {
        const QString k = foldText(name);
        if (k.isEmpty() || k == QLatin1String("various artists") || k == QLatin1String("unknown artist")) return nullptr;
        auto it = byKey.find(k);
        if (it == byKey.end()) { Artist a; a.name = name; a.key = k; it = byKey.insert(k, a); order << k; }
        return &it.value();
    };
    for (int i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        QSet<QString> done;
        for (const QString& n : splitArtists(t.artist) + splitArtists(t.albumArtist)) {
            Artist* a = artistFor(n);
            if (!a || done.contains(a->key)) continue;
            done.insert(a->key);
            a->tracks.push_back(i);
            a->durationMs += t.durationMs;
            if (t.lossless) a->losslessTracks++;
            for (const QString& g : t.genre.split(QRegularExpression("[;,/]"), Qt::SkipEmptyParts)) a->genres.insert(foldText(g.trimmed()));
            if (a->coverKey.isEmpty()) a->coverKey = t.coverKey;
        }
    }
    for (int ai = 0; ai < albums.size(); ++ai) {
        QSet<QString> owners;
        for (const QString& n : splitArtists(albums[ai].artist)) if (Artist* a = artistFor(n)) { a->albums.push_back(ai); owners.insert(a->key); if (!albums[ai].coverKey.isEmpty()) a->coverKey = albums[ai].coverKey; }
        for (int ti : albums[ai].tracks) {
            for (const QString& n : splitArtists(tracks[ti].artist)) {
                Artist* a = artistFor(n);
                if (a && !owners.contains(a->key) && !a->appearsOn.contains(ai)) a->appearsOn.push_back(ai);
            }
        }
    }
    QVector<Artist> artists;
    std::sort(order.begin(), order.end());
    for (const QString& k : order) { m_artistByKey.insert(k, artists.size()); artists.push_back(byKey[k]); }
    m_artistByKey.clear();
    for (int i = 0; i < artists.size(); ++i) m_artistByKey.insert(artists[i].key, i);

    m_albums.reset(std::move(albums));
    m_artists.reset(std::move(artists));
}

std::optional<Track> Library::trackByPath(const QString& path) const
{
    auto it = m_map.constFind(path);
    if (it != m_map.constEnd()) return *it;
    if (needsResolve(path)) {
        auto r = m_remote.constFind(path);
        if (r != m_remote.constEnd()) return *r;
        if (isCatalogPath(path)) if (const auto t = Db::get().catTrack(catalogId(path))) { m_remote.insert(path, *t); return t; }
    }
    return std::nullopt;
}

void Library::noteRemote(const QVector<Track>& tracks)
{
    QVector<Track> fresh, unseen;
    for (const Track& t : tracks) {
        if (!needsResolve(t.path) || m_map.contains(t.path)) continue;
        if (!m_remote.contains(t.path)) unseen.push_back(t);
        m_remote.insert(t.path, t);
        if (isCatalogPath(t.path)) fresh.push_back(t);          // extension songs are only good for this session...
    }
    Db::get().upsertCatTracks(fresh, false);
    if (m_onRemote && !unseen.isEmpty()) m_onRemote(unseen);    // ...but the song index remembers them for good
}

void Library::updateRemote(const Track& t)
{
    if (!needsResolve(t.path) || m_map.contains(t.path)) return;
    m_remote.insert(t.path, t);
}

void Library::importCatalog(const QVector<Track>& tracks)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVector<Track> add;
    for (Track t : tracks) {
        if (!isCatalogPath(t.path)) continue;
        t.added = m_map.contains(t.path) ? m_map.value(t.path).added : now;
        m_map.insert(t.path, t);
        m_remote.remove(t.path);
        add.push_back(t);
    }
    if (add.isEmpty()) return;
    Db::get().upsertCatTracks(add, true);
    publish();
}

void Library::removeCatalog(const QStringList& paths)
{
    QStringList ids;
    for (const QString& p : paths) {
        if (!isCatalogPath(p) || !m_map.contains(p)) continue;
        m_remote.insert(p, m_map.value(p));          // still known (playlists, history), just not in the library
        m_map.remove(p);
        ids << catalogId(p);
    }
    if (ids.isEmpty()) return;
    Db::get().unsaveCatTracks(ids);
    publish();
}

std::optional<Track> Library::matchLocal(const QString& title, const QString& artist, qint64 durationMs) const
{
    const QString k = normTitleKey(title) + QChar(0x1f) + primaryArtistKey(artist);
    const auto it = m_localKey.constFind(k);
    if (it == m_localKey.constEnd()) return std::nullopt;
    const auto t = m_map.constFind(it.value());
    if (t == m_map.constEnd()) return std::nullopt;
    if (variantTag(t->title) != variantTag(title)) return std::nullopt;
    if (durationMs > 0 && t->durationMs > 0 && std::abs(t->durationMs - durationMs) > 12000) return std::nullopt;
    return *t;
}

QVector<Track> Library::songsInView() const
{
    QVector<Track> out;
    out.reserve(m_songsView.rowCount());
    for (int r = 0; r < m_songsView.rowCount(); ++r)
        out.push_back(m_tracks.tracks().at(m_songsView.mapToSource(m_songsView.index(r, 0)).row()));
    return out;
}

QVector<Track> Library::albumTracks(const QString& key) const
{
    QVector<Track> out;
    const int ai = m_albumByKey.value(key, -1);
    if (ai < 0) return out;
    for (int i : m_albums.albums().at(ai).tracks) out.push_back(m_tracks.tracks().at(i));
    return out;
}

QStringList Library::albumPaths(const QString& key) const
{
    QStringList out;
    for (const Track& t : albumTracks(key)) out << t.path;
    return out;
}

QVariantMap Library::albumInfo(const QString& key) const
{
    const int ai = m_albumByKey.value(key, -1);
    if (ai < 0) return {};
    const Album& a = m_albums.albums().at(ai);
    QString quality;
    if (a.lossless) quality = a.hires ? QStringLiteral("Hi-Res Lossless") : QStringLiteral("Lossless");
    else quality = a.codecs.size() == 1 ? a.codecs.first() : QStringLiteral("Mixed formats");
    return {{"key", a.key}, {"title", a.title}, {"artist", a.artist}, {"year", a.year}, {"count", a.tracks.size()},
            {"durationMs", a.durationMs}, {"cover", a.coverKey}, {"lossless", a.lossless}, {"quality", quality}};
}

QVariantMap Library::trackMap(const Track& t) const
{
    return {{"path", t.path}, {"title", t.title}, {"artist", t.artist}, {"album", t.album}, {"trackNo", t.trackNo},
            {"durationMs", t.durationMs}, {"quality", t.quality()}, {"lossless", t.lossless}, {"cover", t.coverKey},
            {"plays", m_stats.count(t.path)}, {"genre", t.genre},
            {"catalog", isCatalogPath(t.path)}, {"artistId", t.artistId}, {"albumId", t.albumId}, {"saved", m_map.contains(t.path)}};
}

QVariantList Library::albumTrackList(const QString& key) const
{
    QVariantList out;
    for (const Track& t : albumTracks(key)) out.push_back(trackMap(t));
    return out;
}

QVariantMap Library::trackInfo(const QString& path) const
{
    const auto t = trackByPath(path);
    if (!t) return {};
    QVariantMap m = trackMap(*t);
    m["albumKey"] = foldText(t->albumArtist) + QChar(0x1f) + foldText(t->album);
    m["artistName"] = splitArtists(t->artist).value(0, t->artist);
    return m;
}

QVariantList Library::tracksInfo(const QStringList& paths) const
{
    QVariantList out;
    for (const QString& p : paths) if (const auto t = trackByPath(p)) out << trackMap(*t);
    return out;
}
