#pragma once
#include "PluginManager.h"
#include "Player.h"
#include <QObject>
#include <QSet>
#include <QTimer>

// Pushes "what is playing" to extensions with the `presence` capability (Discord Rich Presence and
// the like). Changes are coalesced for a moment, so skipping through songs or dragging the seek bar
// makes one update, not twenty (Discord rate-limits activity updates). A paused or stopped player, a
// song that is still being looked up, and an extension that was just switched off all clear the status.
// Nothing is sent unless the user has enabled a presence extension.
class Presence : public QObject {
    Q_OBJECT
public:
    Presence(Player* player, PluginManager* plugins, QObject* parent = nullptr);
    void shutdown();          // clear the status and wait briefly for the answer (call while quitting)

private:
    void schedule();
    void flush();
    void clearAll(const QStringList& ids, bool wait);
    QStringList targets() const { return m_plugins->enabledWith("presence"); }

    Player* m_player;
    PluginManager* m_plugins;
    QTimer m_debounce;
    QSet<QString> m_active;          // extensions that currently show our status
    QString m_lastKey;               // what was sent last (to skip duplicates)
    qint64 m_lastStartMs = 0;        // wall-clock start of the last update, to notice seeks
    bool m_warned = false;
};
