#pragma once
#include "AudioEngine.h"
#include "Library.h"
#include "PluginManager.h"
#include <QHash>
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
    Q_PROPERTY(int repeat READ repeat NOTIFY modeChanged)     // 0 off, 1 all, 2 one
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    Player(AudioEngine* engine, Library* lib, PluginManager* plugins, QObject* parent = nullptr);

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
    QString error() const { return m_error; }

    Q_INVOKABLE void playSong(int songProxyRow);       // queue = current Songs view
    Q_INVOKABLE void playAlbum(int albumProxyRow, int startIndex = 0, bool shuffled = false);
    Q_INVOKABLE void playPaths(const QStringList& paths, int start);   // e.g. search results
    Q_INVOKABLE void playRemote(const QString& pluginId, const QVariantMap& item);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void jumpTo(int upNextIndex);

signals:
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

private:
    const Track* cur() const { return m_pos >= 0 && m_pos < m_order.size() ? &m_items[m_order[m_pos]] : nullptr; }
    void setQueue(QVector<Track> items, int start);
    void buildOrder(int firstItem);
public:
    void saveSession();
    void restoreSession();
private:
    void startAt(int orderPos, qint64 startMs = 0, bool autoplay = true);
    void announceTrack();
    int followingPos(int pos) const;           // -1 when the queue ends
    void queueFollowing(int afterPos);
    void tick();
    void onNextConsumed(qint64 token);
    void setPlaying(bool p);
    void applyGain();

    AudioEngine* m_engine;
    Library* m_lib;
    PluginManager* m_plugins;
    bool m_resolving = false;
    QTimer m_save;
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
    double m_volume = 1.0;
    QString m_error;
    QTimer m_timer;
    QTimer m_idle;
};
