#pragma once
#include "Db.h"
#include "Library.h"
#include <QObject>
#include <QVariantList>

// User playlists (plus the built-in "Liked songs"), stored in SQLite.
class Playlists : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString likedId READ likedId NOTIFY changed)
public:
    explicit Playlists(Library* lib, QObject* parent = nullptr);
    QVariantList list() const;
    int revision() const { return m_rev; }
    QString likedId() const { return QString::number(m_liked); }

    Q_INVOKABLE QString create(const QString& name, const QStringList& paths = {});
    Q_INVOKABLE void rename(const QString& id, const QString& name);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE int add(const QString& id, const QStringList& paths);        // skips songs already there
    int importExternal(const QString& name, const QVector<Db::PlItem>& items, bool likedSongs = false);
    Q_INVOKABLE void removeAt(const QString& id, int index);
    Q_INVOKABLE void move(const QString& id, int from, int to);
    Q_INVOKABLE QVariantMap info(const QString& id) const;
    Q_INVOKABLE QVariantList items(const QString& id) const;
    Q_INVOKABLE QStringList playablePaths(const QString& id) const;
    QStringList likedPaths() const { return QStringList(m_likedSet.begin(), m_likedSet.end()); }
    Q_INVOKABLE bool isLiked(const QString& path) const { return m_likedSet.contains(path); }
    Q_INVOKABLE void toggleLike(const QString& path);
    Q_INVOKABLE QVariantList search(const QString& query) const;

signals:
    void changed();

private:
    struct PL { Db::PlaylistRow row; QVector<Db::PlItem> items; };
    PL* find(qint64 id);
    const PL* find(qint64 id) const;
    void save(PL& p);
    Db::PlItem snapshot(const QString& path) const;
    QStringList coversOf(const PL& p) const;

    Library* m_lib;
    QVector<PL> m_pl;
    QSet<QString> m_likedSet;
    qint64 m_liked = 0;
    int m_rev = 0;
};
