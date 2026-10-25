#pragma once
#include "PluginManager.h"
#include "Track.h"
#include <QObject>
#include <QTimer>
#include <QVariantList>

// Finds lyrics for the playing track and tracks the current line.
// Order: sidecar .lrc next to the file -> local cache -> lyrics extensions (in
// the user's priority order). Candidates are ranked by how closely their
// title / artist / duration match, so a provider's loose search can't win
// with the wrong song.
class Lyrics : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)       // idle, loading, found, none
    Q_PROPERTY(bool synced READ synced NOTIFY linesChanged)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY linesChanged)  // [{t: ms or -1, text}]
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString source READ source NOTIFY linesChanged)
    Q_PROPERTY(QString matchedTitle READ matchedTitle NOTIFY linesChanged)
public:
    explicit Lyrics(PluginManager* plugins, QObject* parent = nullptr);

    QString state() const { return m_state; }
    bool synced() const { return m_synced; }
    QVariantList lines() const { return m_lines; }
    int current() const { return m_current; }
    QString source() const { return m_source; }
    QString matchedTitle() const { return m_matched; }

    Q_INVOKABLE void retry();                 // bypass cache and ask providers again

    // Pure helpers (also used by tests)
    static QVariantList parseLrc(const QString& text, bool* synced);
    static double similarity(const QString& a, const QString& b);
    static QString normalise(const QString& s);

public slots:
    void request(const Track& t);
    void setPosition(qint64 ms);

signals:
    void stateChanged();
    void linesChanged();
    void currentChanged();

private:
    struct Candidate { QString provider, title, artist, synced, plain; double score = 0; };
    void run(bool useCache);
    void tryProvider(int index, int gen);
    void apply(const QString& provider, const QString& body, bool synced, const QString& matched);
    void finishNone(int gen);
    QString cacheKey() const;

    PluginManager* m_plugins;
    Track m_track;
    QString m_state = QStringLiteral("idle");
    QVariantList m_lines;
    QString m_source, m_matched;
    bool m_synced = false;
    int m_current = -1;
    int m_gen = 0;
    qint64 m_posMs = 0;
    QStringList m_providers;
    QTimer m_debounce;
};
