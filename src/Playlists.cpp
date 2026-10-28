#include "Playlists.h"
#include "Covers.h"
#include "TextUtil.h"
#include <QFileInfo>
#include <QSet>

Playlists::Playlists(Library* lib, QObject* parent) : QObject(parent), m_lib(lib)
{
    Db& db = Db::get();
    for (const auto& r : db.playlists()) m_pl.push_back({r, db.playlistItems(r.id)});
    for (const PL& p : std::as_const(m_pl)) if (p.row.special == QLatin1String("liked")) m_liked = p.row.id;
    if (!m_liked) {
        m_liked = db.createPlaylist(QStringLiteral("Liked songs"), QStringLiteral("liked"));
        m_pl.push_front({{m_liked, QStringLiteral("Liked songs"), QStringLiteral("liked"), 0}, {}});
    }
    if (PL* l = find(m_liked)) for (const auto& it : std::as_const(l->items)) m_likedSet.insert(it.path);
}

Playlists::PL* Playlists::find(qint64 id) { for (PL& p : m_pl) if (p.row.id == id) return &p; return nullptr; }
const Playlists::PL* Playlists::find(qint64 id) const { for (const PL& p : m_pl) if (p.row.id == id) return &p; return nullptr; }

void Playlists::save(PL& p)
{
    Db::get().setPlaylistItems(p.row.id, p.items);
    ++m_rev;
    emit changed();
}

Db::PlItem Playlists::snapshot(const QString& path) const
{
    if (auto t = m_lib->trackByPath(path)) return {path, t->title, t->artist, t->album, t->durationMs};
    return {path, QFileInfo(path).completeBaseName(), {}, {}, 0};
}

QStringList Playlists::coversOf(const PL& p) const
{
    QStringList out;
    QSet<QString> seen;
    for (const auto& it : p.items) {
        const auto t = m_lib->trackByPath(it.path);
        if (!t) continue;
        const QString k = t->albumArtist + QChar(0x1f) + t->album;
        if (seen.contains(k)) continue;
        seen.insert(k);
        out << t->coverKey;
        if (out.size() == 4) break;
    }
    return out;
}

QVariantMap Playlists::info(const QString& idStr) const
{
    const PL* p = find(idStr.toLongLong());
    if (!p) return {};
    qint64 dur = 0;
    for (const auto& it : p->items) dur += it.durationMs;
    return {{"id", QString::number(p->row.id)}, {"name", p->row.name}, {"special", p->row.special},
            {"count", p->items.size()}, {"durationMs", dur}, {"covers", coversOf(*p)}};
}

QVariantList Playlists::list() const
{
    QVariantList out;
    for (const PL& p : m_pl) out << info(QString::number(p.row.id));
    return out;
}

QString Playlists::create(const QString& name, const QStringList& paths)
{
    QString n = name.trimmed();
    if (n.isEmpty()) n = tr("New playlist");
    const qint64 id = Db::get().createPlaylist(n);
    PL p{{id, n, {}, 0}, {}};
    for (const QString& path : paths) p.items.push_back(snapshot(path));
    m_pl.push_back(p);
    save(m_pl.last());
    return QString::number(id);
}

void Playlists::rename(const QString& id, const QString& name)
{
    PL* p = find(id.toLongLong());
    const QString n = name.trimmed();
    if (!p || p->row.special == QLatin1String("liked") || n.isEmpty()) return;
    p->row.name = n;
    Db::get().renamePlaylist(p->row.id, n);
    ++m_rev;
    emit changed();
}

void Playlists::remove(const QString& id)
{
    PL* p = find(id.toLongLong());
    if (!p || p->row.special == QLatin1String("liked")) return;
    Db::get().deletePlaylist(p->row.id);
    m_pl.removeIf([&](const PL& x) { return x.row.id == p->row.id; });
    ++m_rev;
    emit changed();
}

int Playlists::add(const QString& id, const QStringList& paths)
{
    PL* p = find(id.toLongLong());
    if (!p) return 0;
    QSet<QString> have;
    for (const auto& it : std::as_const(p->items)) have.insert(it.path);
    int added = 0;
    for (const QString& path : paths) {
        if (have.contains(path)) continue;
        have.insert(path);
        p->items.push_back(snapshot(path));
        if (p->row.id == m_liked) m_likedSet.insert(path);
        ++added;
    }
    if (added) save(*p);
    return added;
}

void Playlists::removeAt(const QString& id, int index)
{
    PL* p = find(id.toLongLong());
    if (!p || index < 0 || index >= p->items.size()) return;
    if (p->row.id == m_liked) m_likedSet.remove(p->items[index].path);
    p->items.removeAt(index);
    save(*p);
}

void Playlists::move(const QString& id, int from, int to)
{
    PL* p = find(id.toLongLong());
    if (!p || from < 0 || to < 0 || from >= p->items.size() || to >= p->items.size() || from == to) return;
    p->items.move(from, to);
    save(*p);
}

QVariantList Playlists::items(const QString& id) const
{
    QVariantList out;
    const PL* p = find(id.toLongLong());
    if (!p) return out;
    for (const auto& it : p->items) {
        const auto t = m_lib->trackByPath(it.path);
        out.push_back(QVariantMap{{"path", it.path}, {"title", t ? t->title : it.title}, {"artist", t ? t->artist : it.artist},
                                  {"album", t ? t->album : it.album}, {"durationMs", t ? t->durationMs : it.durationMs},
                                  {"cover", t ? t->coverKey : QString()}, {"quality", t ? t->quality() : QString()},
                                  {"lossless", t && t->lossless}, {"missing", !t.has_value()}});
    }
    return out;
}

QStringList Playlists::playablePaths(const QString& id) const
{
    QStringList out;
    if (const PL* p = find(id.toLongLong()))
        for (const auto& it : p->items) if (m_lib->trackByPath(it.path)) out << it.path;
    return out;
}

void Playlists::toggleLike(const QString& path)
{
    PL* p = find(m_liked);
    if (!p) return;
    if (m_likedSet.contains(path)) {
        for (int i = 0; i < p->items.size(); ++i) if (p->items[i].path == path) { removeAt(QString::number(m_liked), i); break; }
    } else {
        add(QString::number(m_liked), {path});
    }
}

QVariantList Playlists::search(const QString& query) const
{
    const QStringList tokens = foldText(query).split(' ', Qt::SkipEmptyParts);
    QVariantList out;
    if (tokens.isEmpty()) return out;
    for (const PL& p : m_pl) {
        const QString name = foldText(p.row.name);
        bool all = true;
        for (const QString& t : tokens) if (!name.contains(t)) { all = false; break; }
        if (all) out << info(QString::number(p.row.id));
    }
    return out;
}
