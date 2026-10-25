#include "Library.h"
#include "Db.h"
#include "Probe.h"
#include <QDir>
#include <QDirIterator>
#include <QUrl>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

// ---- TrackModel -------------------------------------------------------------
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
    }
    return {};
}

QHash<int, QByteArray> TrackModel::roleNames() const
{
    return {{TitleRole, "title"}, {ArtistRole, "artist"}, {AlbumRole, "album"}, {DurationRole, "durationMs"},
            {QualityRole, "quality"}, {LosslessRole, "lossless"}, {TrackNoRole, "trackNo"},
            {CoverRole, "cover"}, {PathRole, "path"}};
}

void TrackModel::append(const QVector<Track>& t)
{
    if (t.isEmpty()) return;
    beginInsertRows({}, m_tracks.size(), m_tracks.size() + t.size() - 1);
    m_tracks += t;
    endInsertRows();
}

void TrackModel::reset(QVector<Track> t)
{
    beginResetModel();
    m_tracks = std::move(t);
    endResetModel();
}

// ---- AlbumModel -------------------------------------------------------------
QVariant AlbumModel::data(const QModelIndex& i, int role) const
{
    if (!i.isValid() || i.row() >= m_albums.size()) return {};
    const Album& a = m_albums[i.row()];
    switch (role) {
    case TitleRole:    return a.title;
    case ArtistRole:   return a.artist;
    case YearRole:     return a.year;
    case CountRole:    return a.tracks.size();
    case CoverRole:    return a.coverKey;
    case LosslessRole: return a.lossless;
    }
    return {};
}

QHash<int, QByteArray> AlbumModel::roleNames() const
{
    return {{TitleRole, "title"}, {ArtistRole, "artist"}, {YearRole, "year"}, {CountRole, "count"},
            {CoverRole, "cover"}, {LosslessRole, "lossless"}};
}

void AlbumModel::reset(QVector<Album> a)
{
    beginResetModel();
    m_albums = std::move(a);
    endResetModel();
}

// ---- FilterProxy ------------------------------------------------------------
FilterProxy::FilterProxy(QList<int> roles, QObject* parent) : QSortFilterProxyModel(parent), m_roles(std::move(roles))
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &FilterProxy::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &FilterProxy::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &FilterProxy::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &FilterProxy::countChanged);
}

void FilterProxy::setFilter(const QString& f)
{
    if (f == m_filter) return;
    m_filter = f;
    m_tokens = f.toLower().split(' ', Qt::SkipEmptyParts);
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}

bool FilterProxy::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (m_tokens.isEmpty()) return true;
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    QString hay;
    for (int r : m_roles) hay += sourceModel()->data(idx, r).toString().toLower() + QLatin1Char('\n');
    for (const QString& tok : m_tokens)
        if (!hay.contains(tok)) return false;
    return true;
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
Library::Library(QObject* parent)
    : QObject(parent),
      m_songsView({TrackModel::TitleRole, TrackModel::ArtistRole, TrackModel::AlbumRole}),
      m_albumsView({AlbumModel::TitleRole, AlbumModel::ArtistRole})
{
    qRegisterMetaType<Signatures>("Signatures");
    m_songsView.setSourceModel(&m_tracks);
    m_albumsView.setSourceModel(&m_albums);

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

void Library::setSearch(const QString& s)
{
    if (s == m_search) return;
    m_search = s;
    m_songsView.setFilter(s);
    m_albumsView.setFilter(s);
    emit searchChanged();
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
    // A parent of existing folders replaces them (their tracks stay cached).
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
        if (!covered(it.key())) gone << it.key();
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
    m_scanned = 0;
    m_pending.clear();
    emit scanningChanged();
    emit scanProgress();
    QMetaObject::invokeMethod(m_scanner, "run", Qt::QueuedConnection,
                              Q_ARG(QStringList, m_folders), Q_ARG(Signatures, Db::get().signatures()));
}

void Library::onBatch(const QVector<Track>& t)
{
    for (const Track& x : t) m_map.insert(x.path, x);
    m_pending += t;
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
    m_tracks.reset(std::move(sorted));
    rebuildAlbums();
    emit changed();
}

void Library::rebuildAlbums()
{
    QHash<QString, int> index;
    QVector<Album> albums;
    const auto& tracks = m_tracks.tracks();
    for (int i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        const QString key = t.albumArtist.toLower() + QChar(0x1f) + t.album.toLower();
        auto it = index.find(key);
        if (it == index.end()) {
            Album a;
            a.title = t.album; a.artist = t.albumArtist; a.year = t.year; a.coverKey = t.coverKey;
            it = index.insert(key, albums.size());
            albums.push_back(a);
        }
        Album& a = albums[it.value()];
        a.tracks.push_back(i);
        a.durationMs += t.durationMs;
        a.lossless = a.lossless && t.lossless;
        if (!a.year) a.year = t.year;
    }
    m_albums.reset(std::move(albums));
}

std::optional<Track> Library::trackByPath(const QString& path) const
{
    auto it = m_map.constFind(path);
    if (it == m_map.constEnd()) return std::nullopt;
    return *it;
}

QVector<Track> Library::songsInView() const
{
    QVector<Track> out;
    out.reserve(m_songsView.rowCount());
    for (int r = 0; r < m_songsView.rowCount(); ++r)
        out.push_back(m_tracks.tracks().at(m_songsView.mapToSource(m_songsView.index(r, 0)).row()));
    return out;
}

QVector<Track> Library::albumTracks(int row) const
{
    QVector<Track> out;
    const int src = m_albumsView.mapToSource(m_albumsView.index(row, 0)).row();
    if (src < 0) return out;
    for (int i : m_albums.albums().at(src).tracks) out.push_back(m_tracks.tracks().at(i));
    return out;
}

QVariantMap Library::albumInfo(int row) const
{
    const int src = m_albumsView.mapToSource(m_albumsView.index(row, 0)).row();
    if (src < 0) return {};
    const Album& a = m_albums.albums().at(src);
    QSet<QString> codecs;
    for (int i : a.tracks) codecs.insert(m_tracks.tracks().at(i).codec);
    const QString quality = a.lossless ? QStringLiteral("Lossless")
                                       : (codecs.size() == 1 ? *codecs.begin() : QStringLiteral("Mixed formats"));
    return {{"title", a.title}, {"artist", a.artist}, {"year", a.year}, {"count", a.tracks.size()},
            {"durationMs", a.durationMs}, {"cover", a.coverKey}, {"lossless", a.lossless}, {"quality", quality}};
}

QVariantList Library::albumTrackList(int row) const
{
    QVariantList out;
    for (const Track& t : albumTracks(row))
        out.push_back(QVariantMap{{"title", t.title}, {"artist", t.artist}, {"trackNo", t.trackNo},
                                  {"durationMs", t.durationMs}, {"quality", t.quality()}, {"lossless", t.lossless}});
    return out;
}

QVariantMap Library::searchLocal(const QString& query, int songLimit, int albumLimit) const
{
    const QStringList tokens = query.toLower().split(' ', Qt::SkipEmptyParts);
    QVariantList songs, albums;
    if (tokens.isEmpty()) return {{"songs", songs}, {"albums", albums}};

    struct Hit { int score; int idx; };
    QVector<Hit> hits;
    const auto& tr = m_tracks.tracks();
    for (int i = 0; i < tr.size(); ++i) {
        const Track& t = tr[i];
        const QString title = t.title.toLower();
        const QString hay = title + '\n' + t.artist.toLower() + '\n' + t.album.toLower();
        bool all = true;
        for (const QString& tok : tokens) if (!hay.contains(tok)) { all = false; break; }
        if (!all) continue;
        int score = 0;
        if (title == query.toLower()) score += 100;
        else if (title.startsWith(tokens.first())) score += 40;
        else if (title.contains(tokens.first())) score += 20;
        hits.push_back({score, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    for (int i = 0; i < hits.size() && i < songLimit; ++i) {
        const Track& t = tr[hits[i].idx];
        songs.push_back(QVariantMap{{"path", t.path}, {"title", t.title}, {"artist", t.artist}, {"album", t.album},
                                    {"cover", t.coverKey}, {"quality", t.quality()}, {"lossless", t.lossless},
                                    {"durationMs", t.durationMs}});
    }
    const auto& al = m_albums.albums();
    for (int i = 0; i < al.size() && albums.size() < albumLimit; ++i) {
        const QString hay = al[i].title.toLower() + '\n' + al[i].artist.toLower();
        bool all = true;
        for (const QString& tok : tokens) if (!hay.contains(tok)) { all = false; break; }
        if (all) albums.push_back(QVariantMap{{"row", i}, {"title", al[i].title}, {"artist", al[i].artist}, {"cover", al[i].coverKey}});
    }
    return {{"songs", songs}, {"albums", albums}, {"totalSongs", hits.size()}};
}
