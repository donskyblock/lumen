#include "PluginManager.h"
#include "Db.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QDebug>
#include <signal.h>

// ---- PluginProcess ---------------------------------------------------------------

PluginProcess::PluginProcess(const PluginInfo& info, QObject* parent) : QObject(parent), m_info(info)
{
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &PluginProcess::onReadyRead);
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        const QString e = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
        if (!e.isEmpty()) { m_lastErr = e.right(300); qWarning().noquote() << "[plugin" << m_info.id << "]" << e; }
    });
    connect(&m_proc, &QProcess::started, this, [this] {
        m_started = true;
        m_status = QStringLiteral("Running");
        for (const QByteArray& l : std::as_const(m_outbox)) m_proc.write(l);
        m_outbox.clear();
        m_mem.start();
        emit statusChanged();
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_status = tr("Couldn't start \"%1\". Is it installed?").arg(m_info.exec.value(0));
            failAll(m_status);
            emit statusChanged();
        }
    });
    connect(&m_proc, &QProcess::finished, this, [this] {
        m_mem.stop();
        m_pssKB = 0;
        if (m_status == QLatin1String("Running")) {
            m_status = m_lastErr.isEmpty() ? QStringLiteral("Idle") : tr("Exited: %1").arg(m_lastErr);
            failAll(tr("The extension exited unexpectedly."));
        }
        emit statusChanged();
    });
    m_idle.setSingleShot(true);
    m_idle.setInterval(60000);
    connect(&m_idle, &QTimer::timeout, this, [this] { if (m_pending.isEmpty()) stop(QStringLiteral("Idle")); else m_idle.start(); });
    m_mem.setInterval(3000);
    connect(&m_mem, &QTimer::timeout, this, &PluginProcess::checkMemory);
}

PluginProcess::~PluginProcess()
{
    m_proc.disconnect(this);
    if (running()) { m_proc.kill(); m_proc.waitForFinished(500); }
}

void PluginProcess::startIfNeeded()
{
    if (running()) return;
    m_started = false;
    m_buf.clear();
    m_lastErr.clear();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONDONTWRITEBYTECODE", "1");
    env.insert("LUMEN_PLUGIN_DIR", m_info.dir);
    m_proc.setProcessEnvironment(env);
    m_proc.setWorkingDirectory(m_info.dir);
    QStringList args = m_info.exec;
    const QString prog = args.takeFirst();
    m_status = QStringLiteral("Starting\u2026");
    m_proc.start(prog, args);
    emit statusChanged();
}

void PluginProcess::call(const QString& method, const QJsonObject& params, PluginReply cb, int timeoutMs, PluginPartial partial)
{
    if (m_info.exec.isEmpty()) { cb(false, {}, tr("This extension has no \"exec\" command.")); return; }
    startIfNeeded();
    const int id = m_nextId++;
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] { finish(id, false, {}, tr("The extension took too long to answer.")); });
    timer->start(timeoutMs);
    m_pending.insert(id, {std::move(cb), std::move(partial), timer});
    const QByteArray line = QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact) + '\n';
    if (m_started) m_proc.write(line); else m_outbox << line;
    m_idle.start();
}

void PluginProcess::onReadyRead()
{
    m_buf += m_proc.readAllStandardOutput();
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonParseError pe;
        const QJsonObject o = QJsonDocument::fromJson(line, &pe).object();
        if (pe.error != QJsonParseError::NoError || !o.contains("id")) continue;
        const int id = o.value("id").toInt();
        if (o.value("partial").toBool()) {
            const auto it = m_pending.constFind(id);
            if (it != m_pending.constEnd() && it->partial) it->partial(o.value("result"));
            continue;
        }
        if (o.contains("error")) finish(id, false, {}, o.value("error").toObject().value("message").toString(tr("Unknown error")));
        else finish(id, true, o.value("result"), {});
    }
    m_idle.start();
}

void PluginProcess::finish(int id, bool ok, const QJsonValue& v, const QString& err)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end()) return;
    Pending p = *it;
    m_pending.erase(it);
    p.timer->deleteLater();
    p.cb(ok, v, err);
}

void PluginProcess::failAll(const QString& why)
{
    const auto ids = m_pending.keys();
    for (int id : ids) finish(id, false, {}, why);
}

void PluginProcess::stop(const QString& reason)
{
    if (!reason.isEmpty()) m_status = reason;
    failAll(reason.isEmpty() ? tr("Stopped") : reason);
    m_idle.stop();
    m_mem.stop();
    if (running()) { m_proc.terminate(); if (!m_proc.waitForFinished(800)) m_proc.kill(); }
    m_pssKB = 0;
    emit statusChanged();
}

void PluginProcess::checkMemory()
{
    const qint64 pid = m_proc.processId();
    if (pid <= 0) return;
    QFile f(QStringLiteral("/proc/%1/smaps_rollup").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const QByteArray& l : f.readAll().split('\n')) {
        if (!l.startsWith("Pss:")) continue;
        m_pssKB = l.mid(4).trimmed().split(' ').value(0).toLongLong();
        break;
    }
    if (m_info.memLimitMB > 0 && m_pssKB > qint64(m_info.memLimitMB) * 1024) {
        const QString msg = tr("Stopped: used %1 MB, limit is %2 MB").arg(m_pssKB / 1024).arg(m_info.memLimitMB);
        m_proc.disconnect(this);
        stop(msg);
        // Re-arm the exit handler for the next start.
        connect(&m_proc, &QProcess::finished, this, [this] { m_mem.stop(); emit statusChanged(); });
        return;
    }
    emit statusChanged();
}

// ---- PluginManager ---------------------------------------------------------------

PluginManager::PluginManager(QObject* parent) : QObject(parent)
{
    m_userDir = QDir(Db::get().dataDir()).filePath("plugins");
    QDir().mkpath(m_userDir);
    rescan();
}

void PluginManager::rescan()
{
    for (PluginProcess* p : std::as_const(m_proc)) p->stop();
    qDeleteAll(m_proc);
    m_proc.clear();
    m_info.clear();

    QStringList official;
    for (const QString& d : qEnvironmentVariable("LUMEN_PLUGIN_DIR").split(':', Qt::SkipEmptyParts)) official << d;
    const QString app = QCoreApplication::applicationDirPath();
    official << app + "/plugins" << app + "/../share/lumen/plugins";

    QStringList discovered;
    auto scanDir = [&](const QString& root, bool isOfficial) {
        const QDir d(root);
        for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QFile f(d.filePath(sub + "/plugin.json"));
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            PluginInfo i;
            i.id = o.value("id").toString();
            if (i.id.isEmpty() || m_info.contains(i.id)) continue;
            i.name = o.value("name").toString(i.id);
            i.version = o.value("version").toString();
            i.description = o.value("description").toString();
            i.setup = o.value("setup").toString();
            for (const QJsonValue& v : o.value("exec").toArray()) i.exec << v.toString();
            for (const QJsonValue& v : o.value("capabilities").toArray()) i.capabilities << v.toString();
            i.dir = QFileInfo(d.filePath(sub)).canonicalFilePath();
            i.official = isOfficial;
            i.memLimitMB = o.value("memoryLimitMB").toInt(isOfficial ? 20 : 64);
            for (const QJsonValue& ov : o.value("options").toArray()) {
                const QJsonObject oo = ov.toObject();
                const QString key = oo.value("key").toString();
                if (key.isEmpty()) continue;
                i.options.push_back(QVariantMap{{"key", key}, {"label", oo.value("label").toString(key)}, {"hint", oo.value("hint").toString()},
                                                {"type", oo.value("type").toString("text")}, {"choices", oo.value("choices").toArray().toVariantList()}});
            }
            // Official extensions start on, except those that share something about you (Discord status)
            // and say so with "defaultEnabled": false.
            i.enabled = Db::get().kv("plugin." + i.id + ".enabled", (isOfficial && o.value("defaultEnabled").toBool(true)) ? "1" : "0") == "1";
            m_info.insert(i.id, i);
            discovered << i.id;
        }
    };
    for (const QString& r : std::as_const(official)) scanDir(r, true);
    scanDir(m_userDir, false);

    // Stored priority first, then anything new.
    m_order.clear();
    const QJsonArray saved = QJsonDocument::fromJson(Db::get().kv("plugin.order").toUtf8()).array();
    for (const QJsonValue& v : saved) if (m_info.contains(v.toString())) m_order << v.toString();
    for (const QString& id : std::as_const(discovered)) if (!m_order.contains(id)) m_order << id;

    for (const PluginInfo& i : std::as_const(m_info)) {
        auto* p = new PluginProcess(i, this);
        connect(p, &PluginProcess::statusChanged, this, &PluginManager::changed);
        m_proc.insert(i.id, p);
    }
    emit changed();
}

void PluginManager::saveOrder()
{
    QJsonArray a;
    for (const QString& id : std::as_const(m_order)) a << id;
    Db::get().setKv("plugin.order", QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}

static QString optionKv(const QString& id, const QString& key) { return "plugin." + id + ".opt." + key; }

QString PluginManager::optionValue(const QString& id, const QString& key) const
{
    return Db::get().kv(optionKv(id, key), QString());
}

void PluginManager::setOption(const QString& id, const QString& key, const QString& value)
{
    if (!m_info.contains(id)) return;
    Db::get().setKv(optionKv(id, key), value.trimmed());
    if (m_proc.contains(id)) m_proc[id]->stop();         // a fresh process picks the new setting up cleanly
    emit changed();
}

QVariantList PluginManager::optionsWithValues(const PluginInfo& i) const
{
    QVariantList out;
    for (const QVariant& v : i.options) {
        QVariantMap m = v.toMap();
        m["value"] = optionValue(i.id, m.value("key").toString());
        out.push_back(m);
    }
    return out;
}

QVariantList PluginManager::list() const
{
    QVariantList out;
    for (const QString& id : m_order) {
        const PluginInfo& i = m_info[id];
        const PluginProcess* p = m_proc.value(id);
        out.push_back(QVariantMap{{"id", i.id}, {"name", i.name}, {"version", i.version}, {"description", i.description},
                                  {"capabilities", i.capabilities}, {"official", i.official}, {"enabled", i.enabled},
                                  {"status", p ? p->status() : QString()}, {"running", p && p->running()},
                                  {"memMB", p ? double(p->pssKB()) / 1024.0 : 0.0}, {"memLimit", i.memLimitMB},
                                  {"setup", i.setup}, {"dir", i.dir}, {"options", optionsWithValues(i)}});
    }
    return out;
}

void PluginManager::setEnabled(const QString& id, bool on)
{
    if (!m_info.contains(id)) return;
    m_info[id].enabled = on;
    Db::get().setKv("plugin." + id + ".enabled", on ? "1" : "0");
    if (!on && m_proc.contains(id)) m_proc[id]->stop();
    emit changed();
}

void PluginManager::move(const QString& id, int delta)
{
    const int i = m_order.indexOf(id), j = i + delta;
    if (i < 0 || j < 0 || j >= m_order.size()) return;
    m_order.swapItemsAt(i, j);
    saveOrder();
    emit changed();
}

bool PluginManager::uninstall(const QString& id)
{
    const auto it = m_info.constFind(id);
    if (it == m_info.constEnd() || it->official) return false;
    const QString dir = it->dir;
    if (!dir.startsWith(QFileInfo(m_userDir).canonicalFilePath() + QLatin1Char('/'))) return false;   // never touch anything outside our folder
    if (m_proc.contains(id)) m_proc[id]->stop();
    const bool ok = QDir(dir).removeRecursively();
    Db::get().setKv("plugin." + id + ".enabled", "0");
    rescan();
    return ok;
}

void PluginManager::openUserDir() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_userDir)); }

QStringList PluginManager::enabledWith(const QString& cap) const
{
    QStringList out;
    for (const QString& id : m_order)
        if (m_info[id].enabled && m_info[id].capabilities.contains(cap)) out << id;
    return out;
}

QString PluginManager::nameOf(const QString& id) const { return m_info.value(id).name; }

void PluginManager::call(const QString& id, const QString& method, const QJsonObject& params, PluginReply cb, int timeoutMs, PluginPartial partial)
{
    PluginProcess* p = m_proc.value(id);
    if (!p) { cb(false, {}, tr("Unknown extension")); return; }
    // The extension's own settings travel with every call as "_options" (empty ones left out), so a
    // changed setting takes effect on the next request without restarting anything by hand.
    QJsonObject withOptions = params;
    const PluginInfo info = m_info.value(id);
    if (!info.options.isEmpty()) {
        QJsonObject opts;
        for (const QVariant& v : info.options) {
            const QString key = v.toMap().value("key").toString();
            const QString val = optionValue(id, key);
            if (!val.isEmpty()) opts[key] = val;
        }
        if (!opts.isEmpty()) withOptions["_options"] = opts;
    }
    p->call(method, withOptions, std::move(cb), timeoutMs, std::move(partial));
}

void PluginManager::search(const QString& query)
{
    const int gen = ++m_searchGen;
    const QStringList ids = enabledWith("source");
    emit searchStarted(gen, ids);
    for (const QString& id : ids) {
        auto items = std::make_shared<QVariantList>();
        auto append = [items](const QJsonValue& v) {
            for (const QJsonValue& x : v.toArray()) {
                const QVariantMap item = x.toObject().toVariantMap();
                const QString key = item.value("id").toString();
                bool seen = false;
                for (const QVariant& old : std::as_const(*items)) if (old.toMap().value("id").toString() == key) { seen = true; break; }
                if (!seen) *items << item;
            }
        };
        call(id, "source.search", {{"query", query}, {"limit", 10}}, [this, gen, id, items, append](bool ok, const QJsonValue& v, const QString& err) {
            if (gen != m_searchGen) return;
            if (ok) append(v);
            emit searchResult(gen, id, nameOf(id), *items, ok ? QString() : err);
        }, 25000, [this, gen, id, items, append](const QJsonValue& v) {
            if (gen != m_searchGen) return;
            append(v);
            emit searchResult(gen, id, nameOf(id), *items, {});
        });
    }
}
