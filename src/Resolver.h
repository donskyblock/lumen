#pragma once
#include "Catalog.h"
#include "Library.h"
#include "PluginManager.h"
#include <QHash>
#include <QObject>
#include <QSet>
#include <functional>
#include <memory>
#include <optional>

// Finds something to actually play for an online catalog song. Tried in order:
//   1. the same recording in your own library (best quality, works offline)
//   2. "source" extensions you have switched on (full-length streams)
//   3. the catalog's own 30-second preview, which needs no setup at all
struct Resolved {
    bool ok = false;
    bool full = false;                  // false = preview
    QString url, headers, format, via, error;
    bool lossless = false;
    int bitrateKbps = 0, sampleRate = 0, bitDepth = 0;
    qint64 durationMs = 0;              // length of what will actually play
    qint64 at = 0;
    QString providerId, providerItemId; // lets a decoder failure skip one bad result
};

class Resolver : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList fullSources READ fullSources NOTIFY changed)
    Q_PROPERTY(bool hasFullSource READ hasFullSource NOTIFY changed)
public:
    using Done = std::function<void(const Resolved&)>;
    Resolver(Library* lib, PluginManager* plugins, Catalog* catalog, QObject* parent = nullptr);

    QStringList fullSources() const;
    bool hasFullSource() const { return !m_plugins->enabledWith("source").isEmpty(); }
    std::optional<Resolved> cached(const QString& path) const;
    void resolve(const Track& t, Done cb);
    // Called by playback if a resolved provider stream cannot actually be
    // opened. The next resolve skips that exact provider item.
    void reportFailed(const QString& path, const Resolved& resolved);
    // A full source can go stale between resolving and FFmpeg opening it.
    // Catalog tracks still have a reliable fresh-preview fallback.
    void fallbackPreview(const Track& t, Done cb);
    // Look for a full-length source for a song that is playing as a 30 s preview, without falling back
    // to the preview again. `cb` gets ok=false when nothing was found. Pass the song with its REAL length.
    void upgrade(const Track& t, Done cb);
    void forget(const QString& path) { m_cache.remove(path); }

signals:
    void changed();

private:
    // One song being looked up through the "source" extensions: which extension and which search
    // wording we are on, and which results were already tried (so a second wording adds new ones).
    struct Ctx { Track t; QStringList ids; int idx = 0; int pass = 0; QSet<QString> tried; Done upgrade; QString lastError; };
    void tryExtensions(std::shared_ptr<Ctx> c);
    void tryCandidates(std::shared_ptr<Ctx> c, const QString& pluginId, QVector<QVariantMap> cands, int ci, std::function<void()> next);
    void deliver(const std::shared_ptr<Ctx>& c, Resolved r);
    void fallback(const Track& t);         // preview for catalog songs, an error for everything else
    void preview(const Track& t);
    void resolveExt(const Track& t, int attempt = 0);
    bool banned(const QString& path, const QString& pluginId, const QString& itemId) const;
    void finish(const QString& path, Resolved r);

    Library* m_lib;
    PluginManager* m_plugins;
    Catalog* m_catalog;
    QHash<QString, Resolved> m_cache;
    QHash<QString, QVector<Done>> m_waiting;
    QHash<QString, qint64> m_failedCandidates;      // key -> when it failed (bans expire)
};
