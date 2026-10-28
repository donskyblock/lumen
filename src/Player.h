#pragma once
#include "AudioEngine.h"
#include "Library.h"
#include "Metadata.h"
#include "SongIndex.h"
#include "PluginManager.h"
#include "Resolver.h"
#include <QHash>
#include <QSet>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QVariantList>

// Queue + transport state exposed to QML as `Player`.
class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString cover READ cover NOTIFY trackChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY trackChanged)
    Q_PROPERTY(bool resolving READ resolving NOTIFY resolvingChanged)
    Q_PROPERTY(QString quality READ quality NOTIFY trackChanged)
    Q_PROPERTY(QString codec READ codec NOTIFY trackChanged)
    Q_PROPERTY(bool lossless READ lossless NOTIFY trackChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY trackChanged)
    Q_PROPERTY(QVariantList upNext READ upNext NOTIFY queueChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY modeChanged)
    Q_PROPERTY(int repeat READ repeat NOTIFY modeChanged)
    Q_PROPERTY(bool autoplay READ autoplay NOTIFY modeChanged)        // keep going with similar songs when the queue runs out
    Q_PROPERTY(bool autoplayBusy READ autoplayBusy NOTIFY modeChanged)     // 0 off, 1 all, 2 one
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool preview READ isPreview NOTIFY trackChanged)           // playing a 30 s sample of a catalog song
    Q_PROPERTY(QString via READ via NOTIFY trackChanged)                  // where the audio is coming from
public:
    Player(AudioEngine* engine, Library* lib, PluginManager* plugins, QObject* parent = nullptr);
    ~Player() override;

    bool hasTrack() const { return m_pos >= 0; }
    bool playing() const { return m_playing; }
    qint64 position() const { return m_position; }
    qint64 duration() const { return cur() ? cur()->durationMs : 0; }
    QString title() const { return cur() ? cur()->title : QString(); }
    QString artist() const { return cur() ? cur()->artist : QString(); }
    QString album() const { return cur() ? cur()->album : QString(); }
    QString cover() const { return cur() ? cur()->coverKey : QString(); }
    QString coverUrl() const { return cur() ? cur()->coverUrl : QString(); }
    bool resolving() const { return m_resolving; }
    std::optional<Track> currentTrack() const { return cur() ? std::optional<Track>(*cur()) : std::nullopt; }
    QString quality() const { return cur() ? cur()->quality() : QString(); }
    QString codec() const { return cur() ? cur()->codec : QString(); }
    bool lossless() const { return cur() && cur()->lossless; }
    QString currentPath() const { return cur() ? cur()->path : QString(); }
    QVariantList upNext() const;
    double volume() const { return m_volume; }
    void setVolume(double v);
    bool shuffle() const { return m_shuffle; }
    int repeat() const { return m_repeat; }
    bool autoplay() const { return m_autoplay; }
    bool autoplayBusy() const { return m_autoBusy; }
    Q_INVOKABLE void setAutoplay(bool on);
    void autoplayArrived(int ticket, const QStringList& paths, const QStringList& sources);
    QString error() const { return m_error; }
    bool isPreview() const { return cur() && isCatalogPath(cur()->path) && m_curVia == QLatin1String("preview"); }
    QString via() const { return m_curViaName; }
    void setResolver(Resolver* r) { m_resolver = r; }
    void setMetadata(Metadata* m) { m_meta = m; }
    void setIndex(SongIndex* i) { m_index = i; }

    Q_INVOKABLE void playSong(int songProxyRow);       // queue = current Songs view
    Q_INVOKABLE void playAlbum(const QString& albumKey, int startIndex = 0, bool shuffled = false);
    Q_INVOKABLE void playPathsShuffled(const QStringList& paths);
    Q_INVOKABLE void playNext(const QStringList& paths);      // right after the current song
    Q_INVOKABLE void addToQueue(const QStringList& paths);    // end of the queue
    Q_INVOKABLE void playPaths(const QStringList& paths, int start);   // e.g. search results
    Q_INVOKABLE void playRemote(const QString& pluginId, const QVariantMap& item);
    // Play one online song and let the algorithm (your library, the catalog and every extension that
    // recommends) fill Up Next, instead of queueing the whole list the song was clicked in. With
    // autoplay switched off nothing would follow, so then `context` is queued as before.
    // A queue that is exactly the songs given (a playlist): autoplay never adds to it.
    Q_INVOKABLE void playFixed(const QStringList& paths, int start = 0);
    Q_INVOKABLE void markFixed() { m_nextFixed = true; }      // call right BEFORE starting a playlist's queue (shuffled etc.)
    Q_INVOKABLE void playOnlineSong(const QString& path, const QStringList& context = {});
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void jumpTo(int upNextIndex);

signals:
    void skipped(const QString& path);
    void autoplayWanted(int ticket, const QStringList& seeds, const QStringList& exclude);
    void trackChanged();
    void playingChanged();
    void positionChanged();
    void volumeChanged();
    void modeChanged();
    void queueChanged();
    void errorChanged();
    void coverForPalette(const QString& path);
    void paletteReset();
    void trackInfo(const Track& t);
    void resolvingChanged();
    void seeked();                              // the listener jumped to another point in the song
    void problem(const QString& message);
    void notice(const QString& message);        // informational toast (e.g. looking for the full version)      // a song could not be played and was skipped / playback stopped

private:
    const Track* cur() const { return m_pos >= 0 && m_pos < m_order.size() ? &m_items[m_order[m_pos]] : nullptr; }
    void setQueue(QVector<Track> items, int start);
    void buildOrder(int firstItem);
public:
    void saveSession();
    void restoreSession();
private:
    void startAt(int orderPos, qint64 startMs = 0, bool autoplay = true);
    void startResolved(int orderPos, qint64 token, qint64 startMs, bool autoplay, const Resolved& r);
    void noteSkipIfEarly();
    void announceTrack();
    void finishListen();
    void maybeRequestAutoplay();
    Track realTrack(const Track& t) const;       // a catalog song with its true length (a played preview shortens the queue's copy)
    void maybeUpgrade(int orderPos, qint64 token, const QString& path);
    void failUpgrade(const QString& why = {});
    bool handleFailure(qint64 token);            // true when it took over (the caller must not process this poll further)
    void recoverTrack(int orderPos, const Resolved& used, qint64 resumeMs);
    void giveUp(int orderPos, const QString& why);
    bool startEnrich();                          // look up proper metadata for an extension song; true while pending
    void finishEnrich(const QString& path);
    void applyMeta(const QString& path, const SongMeta& m);
    int followingPos(int pos) const;           // -1 when the queue ends
    void queueFollowing(int afterPos);
    void tick();
    void onNextConsumed(qint64 token);
    void setPlaying(bool p);
    void applyGain();
    QString cachedStream(const QString& logicalPath) const;
    void cacheStream(const Track& track, const Resolved& resolved);
    void pruneStreamCache() const;

    Resolver* m_resolver = nullptr;
    Metadata* m_meta = nullptr;
    SongIndex* m_index = nullptr;
    bool m_nextFixed = false;              // the next queue to be set is a playlist
    bool m_fixed = false;                  // the queue is a playlist's own songs: no autoplay
    QSet<QString> m_enriched;
    QString m_enrichPending;
    quint64 m_upgradeSerial = 0;
    bool m_upgrading = false, m_upgradeEnded = false;   // looking for a full version of the preview now playing
    int m_failSkips = 0;                   // consecutive songs skipped because they would not play
    bool m_autoSkipping = false;
    int m_nextFailedPos = -1;              // the gapless-queued next song failed to open
    Resolved m_nextRes;                    // what that queued song was resolved to
    qint64 m_resumeFrom = 0;               // where the current (re)start began, to tell a healthy stream from a flapping one
    quint64 m_startSerial = 0;
    bool m_waitingNext = false;            // the next song is a catalog song whose address isn't known yet
    QString m_curVia, m_curViaName;        // "preview" / "full" and a display name for the current catalog song
    AudioEngine* m_engine;
    Library* m_lib;
    PluginManager* m_plugins;
    QNetworkAccessManager m_cacheNet;
    QHash<QString, QNetworkReply*> m_cacheDownloads;
    bool m_resolving = false;
    int m_streamRetries = 0;
    Resolved m_currentResolved;
    QTimer m_save;
    QString m_listenPath;
    qint64 m_listenMs = 0, m_listenDur = 0;
    QElapsedTimer m_tickClock;
    QVector<Track> m_items;
    QVector<int> m_order;          // play order -> index into m_items
    int m_pos = -1;                // position in m_order of the audible track
    qint64 m_tokenSeq = 0;
    qint64 m_audibleToken = -1;
    QHash<qint64, int> m_posByToken;
    qint64 m_position = 0;
    bool m_playing = false;
    bool m_stopped = true;         // nothing loaded in the engine
    bool m_shuffle = false;
    int m_repeat = 0;
    bool m_autoplay = true, m_autoBusy = false, m_autoAdvance = false;
    int m_autoTicket = 0;
    double m_volume = 1.0;
    QString m_error;
    QTimer m_timer;
    QTimer m_idle;
};
