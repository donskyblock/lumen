#include "Presence.h"
#include <QDateTime>
#include <QEventLoop>
#include <QJsonObject>
#include <QDebug>
#include <cstdlib>
#include <memory>

Presence::Presence(Player* player, PluginManager* plugins, QObject* parent) : QObject(parent), m_player(player), m_plugins(plugins)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(800);
    connect(&m_debounce, &QTimer::timeout, this, &Presence::flush);
    connect(player, &Player::trackChanged, this, &Presence::schedule);
    connect(player, &Player::playingChanged, this, &Presence::schedule);
    connect(player, &Player::resolvingChanged, this, &Presence::schedule);
    connect(player, &Player::seeked, this, &Presence::schedule);
    connect(plugins, &PluginManager::changed, this, [this] {
        // an extension that was switched off must not leave a stale status on the profile
        const QStringList now = targets();
        QStringList gone;
        for (const QString& id : std::as_const(m_active)) if (!now.contains(id)) gone << id;
        if (!gone.isEmpty()) clearAll(gone, false);
        schedule();
    });
}

void Presence::schedule()
{
    if (!targets().isEmpty() || !m_active.isEmpty()) m_debounce.start();
}

void Presence::clearAll(const QStringList& ids, bool wait)
{
    // the callbacks can run after this function returns, so they share their state instead of borrowing the stack
    struct State { QEventLoop loop; int left = 0; };
    auto st = std::make_shared<State>();
    st->left = int(ids.size());
    for (const QString& id : ids) {
        m_active.remove(id);
        m_plugins->call(id, "presence.clear", {}, [st](bool, const QJsonValue&, const QString&) {
            if (--st->left == 0) st->loop.quit();
        }, 3000);
    }
    m_lastKey.clear();
    if (wait && st->left > 0) {
        QTimer::singleShot(1200, &st->loop, &QEventLoop::quit);      // never hold up quitting for a dead extension
        st->loop.exec();
    }
}

void Presence::flush()
{
    const QStringList ids = targets();
    if (ids.isEmpty()) return;
    const bool show = m_player->hasTrack() && m_player->playing() && !m_player->resolving();
    if (!show) {
        if (!m_active.isEmpty()) clearAll(QStringList(m_active.begin(), m_active.end()), false);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 startMs = now - m_player->position();
    const QString key = m_player->currentPath() + QChar(0x1f) + m_player->title() + QChar(0x1f) + m_player->artist()
                      + QChar(0x1f) + m_player->album() + QChar(0x1f) + m_player->coverUrl() + QChar(0x1f) + QString::number(m_player->duration());
    // same song, same details, and playback still where the last update said it would be: nothing to send
    if (key == m_lastKey && std::llabs(startMs - m_lastStartMs) < 2500 && m_active.size() == ids.size()) return;

    const QJsonObject params{{"title", m_player->title()}, {"artist", m_player->artist()}, {"album", m_player->album()},
                             {"cover", m_player->coverUrl()}, {"durationMs", double(m_player->duration())},
                             {"positionMs", double(m_player->position())}, {"playing", true}};
    m_lastKey = key;
    m_lastStartMs = startMs;
    for (const QString& id : ids) {
        m_active.insert(id);
        m_plugins->call(id, "presence.update", params, [this, id](bool ok, const QJsonValue&, const QString& err) {
            if (ok) { m_warned = false; return; }
            m_lastKey.clear();                                   // try again on the next change
            if (!m_warned) { m_warned = true; qWarning().noquote() << "[presence" << id << "]" << err; }
        }, 5000);
    }
}

void Presence::shutdown()
{
    m_debounce.stop();
    if (!m_active.isEmpty()) clearAll(QStringList(m_active.begin(), m_active.end()), true);
}
