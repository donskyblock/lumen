#pragma once
#include "Db.h"
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QVector>
#include <cmath>

// Listening history: counts, recency and a time-decayed score per track.
// A play is recorded after 30 s (or half the track, for short ones).
class PlayStats : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    struct Event { QString path; qint64 at; };

    explicit PlayStats(QObject* parent = nullptr) : QObject(parent) { load(); }

    int revision() const { return m_rev; }
    int count(const QString& path) const { return m_count.value(path); }
    qint64 last(const QString& path) const { return m_last.value(path); }
    double score(const QString& path) const { return m_score.value(path); }
    const QVector<Event>& events() const { return m_events; }
    bool any() const { return !m_events.isEmpty(); }
    double tauDays() const { return m_tauDays; }
    void setTauDays(double d) { if (qFuzzyCompare(d, m_tauDays)) return; m_tauDays = std::max(1.0, d); rebuild(); emit changed(); }
    void reloadFromDb() { m_events.clear(); load(); emit changed(); }

    void record(const QString& path, qint64 listenedMs)
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        Db::get().recordPlay(path, now, listenedMs);
        m_events.push_back({path, now});
        rebuild();
        emit changed();
    }

signals:
    void changed();

private:
    void load()
    {
        const qint64 since = QDateTime::currentSecsSinceEpoch() - 730LL * 86400;
        for (const auto& r : Db::get().plays(since)) m_events.push_back({r.path, r.at});
        rebuild();
    }
    void rebuild()
    {
        m_count.clear(); m_last.clear(); m_score.clear();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const double tau = m_tauDays * 86400.0;           // plays fade over ~6 weeks by default
        for (const Event& e : std::as_const(m_events)) {
            m_count[e.path] += 1;
            m_last[e.path] = std::max(m_last.value(e.path), e.at);
            m_score[e.path] += std::exp(-double(now - e.at) / tau);
        }
        ++m_rev;
    }
    QVector<Event> m_events;
    QHash<QString, int> m_count;
    QHash<QString, qint64> m_last;
    QHash<QString, double> m_score;
    int m_rev = 0;
    double m_tauDays = 45.0;
};
