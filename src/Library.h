#pragma once
#include "Track.h"
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QThread>
#include <QVariantMap>
#include <atomic>
#include <optional>

// ---- tracks ----------------------------------------------------------------
class TrackModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TitleRole = Qt::UserRole + 1, ArtistRole, AlbumRole, DurationRole, QualityRole,
                LosslessRole, TrackNoRole, CoverRole, PathRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_tracks.size(); }
    QVariant data(const QModelIndex& i, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void append(const QVector<Track>& t);
    void reset(QVector<Track> t);
    const QVector<Track>& tracks() const { return m_tracks; }
private:
    QVector<Track> m_tracks;
};

// ---- albums ----------------------------------------------------------------
struct Album {
    QString title, artist, coverKey;
    int year = 0;
    qint64 durationMs = 0;
    bool lossless = true;
    QVector<int> tracks;     // indices into the TrackModel
};

class AlbumModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { TitleRole = Qt::UserRole + 1, ArtistRole, YearRole, CountRole, CoverRole, LosslessRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_albums.size(); }
    QVariant data(const QModelIndex& i, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(QVector<Album> a);
    const QVector<Album>& albums() const { return m_albums; }
private:
    QVector<Album> m_albums;
};

// ---- search filter ----------------------------------------------------------
class FilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit FilterProxy(QList<int> roles, QObject* parent = nullptr);
    QString filter() const { return m_filter; }
    void setFilter(const QString& f);
    int count() const { return rowCount(); }
signals:
    void filterChanged();
    void countChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
private:
    QList<int> m_roles;
    QString m_filter;
    QStringList m_tokens;
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
    Q_PROPERTY(int trackCount READ trackCount NOTIFY changed)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int scanned READ scanned NOTIFY scanProgress)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY foldersChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
public:
    explicit Library(QObject* parent = nullptr);
    ~Library() override;

    QObject* songs() { return &m_songsView; }
    QObject* albums() { return &m_albumsView; }
    int trackCount() const { return m_tracks.rowCount(); }
    int albumCount() const { return m_albums.rowCount(); }
    bool scanning() const { return m_scanning; }
    int scanned() const { return m_scanned; }
    QVariantList folders() const;
    QString folder() const { return m_folders.value(0); }
    QString search() const { return m_search; }
    void setSearch(const QString& s);

    // For the Player
    QVector<Track> songsInView() const;
    QVector<Track> albumTracks(int albumProxyRow) const;
    std::optional<Track> trackByPath(const QString& path) const;

    // Returns an empty string on success, otherwise a message for the user.
    Q_INVOKABLE QString addFolder(const QString& path);
    Q_INVOKABLE void removeFolder(const QString& path);
    Q_INVOKABLE void rescan() { scan(); }
    Q_INVOKABLE QVariantMap albumInfo(int albumProxyRow) const;
    Q_INVOKABLE QVariantList albumTrackList(int albumProxyRow) const;
    Q_INVOKABLE QVariantMap searchLocal(const QString& query, int songLimit = 8, int albumLimit = 8) const;

signals:
    void changed();
    void foldersChanged();
    void scanningChanged();
    void scanProgress();
    void searchChanged();

private:
    void scan();
    void onBatch(const QVector<Track>& t);
    void onDone(const QStringList& removed);
    void publish();                       // map -> sorted models
    void rebuildAlbums();
    bool covered(const QString& path) const;

    TrackModel m_tracks;
    AlbumModel m_albums;
    FilterProxy m_songsView;
    FilterProxy m_albumsView;
    QHash<QString, Track> m_map;
    QVector<Track> m_pending;
    QStringList m_folders;
    QString m_search;
    QThread m_thread;
    Scanner* m_scanner = nullptr;
    bool m_scanning = false;
    bool m_rescanQueued = false;
    int m_scanned = 0;
};
