#pragma once
#include <functional>
#include "PlayStats.h"
#include "Track.h"
#include <QAbstractListModel>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QThread>
#include <QVariantMap>
#include <atomic>
#include <optional>

// ---- tracks -----------------------------------------------------------------
class TrackModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TitleRole = Qt::UserRole + 1, ArtistRole, AlbumRole, DurationRole, QualityRole, LosslessRole,
                TrackNoRole, CoverRole, PathRole, GenreRole, YearRole, AddedRole, PlaysRole, HiResRole, CodecRole };
    using QAbstractListModel::QAbstractListModel;
    void setStats(const PlayStats* s) { m_stats = s; }
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_tracks.size(); }
    QVariant data(const QModelIndex& i, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(QVector<Track> t);
    void statsChanged();
    const QVector<Track>& tracks() const { return m_tracks; }
private:
    QVector<Track> m_tracks;
    const PlayStats* m_stats = nullptr;
};

// ---- albums ----------------------------------------------------------------
struct Album {
    QString key, title, artist, coverKey, folded;
    int year = 0;
    qint64 durationMs = 0, added = 0;
    bool lossless = true, hires = false;
    QStringList codecs;
    QVector<int> tracks;     // indices into the TrackModel
};

class AlbumModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1, TitleRole, ArtistRole, YearRole, CountRole, CoverRole, LosslessRole,
                AddedRole, PlaysRole, HiResRole, CodecRole };
    using QAbstractListModel::QAbstractListModel;
    void setSources(const TrackModel* t, const PlayStats* s) { m_tm = t; m_stats = s; }
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_albums.size(); }
    QVariant data(const QModelIndex& i, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(QVector<Album> a);
    void statsChanged();
    const QVector<Album>& albums() const { return m_albums; }
private:
    QVector<Album> m_albums;
    const TrackModel* m_tm = nullptr;
    const PlayStats* m_stats = nullptr;
};

// ---- artists ----------------------------------------------------------------
struct Artist {
    QString name, key, coverKey;
    QVector<int> albums;       // albums where they are the album artist
    QVector<int> appearsOn;    // albums with a track by them
    QVector<int> tracks;
    QSet<QString> genres;
    qint64 durationMs = 0;
    int losslessTracks = 0;
};

class ArtistModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, AlbumCountRole, CountRole, CoverRole, PlaysRole };
    using QAbstractListModel::QAbstractListModel;
    void setSources(const TrackModel* t, const PlayStats* s) { m_tm = t; m_stats = s; }
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_artists.size(); }
    QVariant data(const QModelIndex& i, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(QVector<Artist> a);
    void statsChanged();
    const QVector<Artist>& artists() const { return m_artists; }
private:
    QVector<Artist> m_artists;
    const TrackModel* m_tm = nullptr;
    const PlayStats* m_stats = nullptr;
};

// ---- sort + quality filters over any of the models ---------------------------
class ViewProxy : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY viewChanged)      // a role name, or "default"
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY viewChanged)
    Q_PROPERTY(bool losslessOnly READ losslessOnly WRITE setLosslessOnly NOTIFY viewChanged)
    Q_PROPERTY(bool hiResOnly READ hiResOnly WRITE setHiResOnly NOTIFY viewChanged)
    Q_PROPERTY(QString codec READ codec WRITE setCodec NOTIFY viewChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit ViewProxy(QObject* parent = nullptr);
    QString sortKey() const { return m_key; }
    bool descending() const { return m_desc; }
    bool losslessOnly() const { return m_lossless; }
    bool hiResOnly() const { return m_hires; }
    QString codec() const { return m_codec; }
    int count() const { return rowCount(); }
    void setSortKey(const QString& k);
    void setDescending(bool d);
    void setLosslessOnly(bool b);
    void setHiResOnly(bool b);
    void setCodec(const QString& c);
signals:
    void viewChanged();
    void countChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& l, const QModelIndex& r) const override;
private:
    void resort();
    int roleId(const char* name) const;
    QString m_key = QStringLiteral("default");
    QString m_codec;
    bool m_desc = false, m_lossless = false, m_hires = false;
};

// ---- background scanner -----------------------------------------------------
using Signatures = QHash<QString, QPair<qint64, qint64>>;   // path -> (mtime, size)

class Scanner : public QObject {
    Q_OBJECT
public:
    std::atomic<bool> cancel{false};
public slots:
    void run(QStringList folders, Signatures known);
signals:
    void batch(QVector<Track> tracks);            // new or changed files only
    void progress(int examined);
    void done(QStringList removed);
};

// ---- library facade exposed to QML as `Library` ----------------------------
class Library : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* songs READ songs CONSTANT)
    Q_PROPERTY(QObject* albums READ albums CONSTANT)
    Q_PROPERTY(QObject* artists READ artists CONSTANT)
    Q_PROPERTY(QObject* stats READ stats CONSTANT)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY changed)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY changed)
    Q_PROPERTY(int artistCount READ artistCount NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QStringList codecs READ codecs NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int scanned READ scanned NOTIFY scanProgress)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
public:
    explicit Library(QObject* parent = nullptr);
    ~Library() override;

    QObject* songs() { return &m_songsView; }
    QObject* albums() { return &m_albumsView; }
    QObject* artists() { return &m_artistsView; }
    PlayStats* stats() { return &m_stats; }
    int trackCount() const { return m_tracks.rowCount(); }
    int albumCount() const { return m_albums.rowCount(); }
    int artistCount() const { return m_artists.rowCount(); }
    int revision() const { return m_rev; }
    QStringList codecs() const { return m_codecs; }
    bool scanning() const { return m_scanning; }
    int scanned() const { return m_scanned; }
    QVariantList folders() const;

    // For the Player and Playlists
    QVector<Track> songsInView() const;
    QVector<Track> albumTracks(const QString& albumKey) const;
    std::optional<Track> trackByPath(const QString& path) const;          // files, saved catalog songs and any catalog song Lumen has shown

    // online catalog songs
    void noteRemote(const QVector<Track>& tracks);                        // remember (not in the library)
    void setRemoteHook(std::function<void(const QVector<Track>&)> hook) { m_onRemote = std::move(hook); }   // told about every remote song first seen
    void updateRemote(const Track& t);                                    // better metadata arrived for a remembered remote song
    void importCatalog(const QVector<Track>& tracks);                     // "Add to library"
    void removeCatalog(const QStringList& paths);
    bool isSaved(const QString& path) const { return m_map.contains(path); }
    // Songs in your library that sound like `seed` (same artist, related artists, same genre), with a 0..1 closeness.
    QVector<QPair<Track, double>> similarLocal(const Track& seed, int n) const;
    // The same recording as a local file, if you own one.
    std::optional<Track> matchLocal(const QString& title, const QString& artist, qint64 durationMs) const;

    // Returns an empty string on success, otherwise a message for the user.
    Q_INVOKABLE QString addFolder(const QString& path);
    Q_INVOKABLE void removeFolder(const QString& path);
    Q_INVOKABLE void rescan() { scan(); }

    Q_INVOKABLE QVariantMap albumInfo(const QString& key) const;
    Q_INVOKABLE QVariantList albumTrackList(const QString& key) const;
    Q_INVOKABLE QStringList albumPaths(const QString& key) const;
    Q_INVOKABLE QVariantMap trackInfo(const QString& path) const;          // for "go to album / artist"
    Q_INVOKABLE QVariantMap artistPage(const QString& name) const;
    Q_INVOKABLE QStringList artistPaths(const QString& name) const;
    Q_INVOKABLE QVariantMap searchAll(const QString& query, bool losslessOnly = false) const;
    // Search "Top result" songs: your library's and the online catalog's best songs for `query`,
    // mixed fairly (most played local songs alternate with the most popular online ones). A song
    // you own wins over an online copy of the same recording. Entries get "online": true/false.
    Q_INVOKABLE QVariantList blendSongs(const QString& query, const QVariantList& local, const QVariantList& online, int n) const;
    Q_INVOKABLE QVariantMap home() const;
    Q_INVOKABLE QStringList mixPaths(const QString& mixId) const;
    Q_INVOKABLE QVariantMap mixInfo(const QString& mixId) const { return buildMix(mixId); }
    Q_INVOKABLE QVariantList tracksInfo(const QStringList& paths) const;

signals:
    void changed();
    void foldersChanged();
    void scanningChanged();
    void scanProgress();

private:
    void scan();
    void onBatch(const QVector<Track>& t);
    void onDone(const QStringList& removed);
    void publish();
    void rebuildAlbumsAndArtists();
    bool covered(const QString& path) const;
    QVariantMap trackMap(const Track& t) const;
    QVector<QPair<QString, double>> relatedArtists(const QString& artistKey, int n) const;
    QVariantMap buildMix(const QString& id) const;

    TrackModel m_tracks;
    AlbumModel m_albums;
    ArtistModel m_artists;
    ViewProxy m_songsView, m_albumsView, m_artistsView;
    PlayStats m_stats;
    QHash<QString, Track> m_map;
    mutable QHash<QString, Track> m_remote;
    std::function<void(const QVector<Track>&)> m_onRemote;     // told about every remote song first seen
    QHash<QString, QString> m_localKey;        // normalised "title|artist" -> file path
    QHash<QString, int> m_albumByKey, m_artistByKey;
    QVector<int> m_trackAlbum;
    QVector<Track> m_pending;
    QStringList m_folders, m_codecs;
    QThread m_thread;
    Scanner* m_scanner = nullptr;
    bool m_scanning = false, m_rescanQueued = false, m_firstScan = false;
    int m_scanned = 0, m_rev = 0;
};
