#include "Marketplace.h"
#include "Db.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>

namespace {
// Placeholder: point this at the index of your own extensions repository.
constexpr const char* kDefaultIndex = "https://raw.githubusercontent.com/YOUR-NAME/lumen-extensions/main/index.json";
}

Marketplace::Marketplace(PluginManager* plugins, QObject* parent) : QObject(parent), m_plugins(plugins)
{
    m_url = Db::get().kv("marketplace.url", qEnvironmentVariable("LUMEN_MARKET_URL", kDefaultIndex));
}

QNetworkAccessManager* Marketplace::net()
{
    if (!m_net) m_net = new QNetworkAccessManager(this);      // Qt Network is only touched when the user opens the store
    return m_net;
}

void Marketplace::setIndexUrl(const QString& u)
{
    const QString t = u.trimmed();
    if (t == m_url) return;
    m_url = t;
    Db::get().setKv("marketplace.url", t);
    emit indexUrlChanged();
}

void Marketplace::setBusy(bool b) { if (m_busy != b) { m_busy = b; emit busyChanged(); } }

void Marketplace::fail(const QString& msg)
{
    m_message = msg;
    emit messageChanged();
    setBusy(false);
}

void Marketplace::refresh()
{
    if (m_busy) return;
    const QUrl url = m_url.startsWith('/') ? QUrl::fromLocalFile(m_url) : QUrl(m_url);
    if (!url.isValid() || url.scheme().isEmpty()) { fail(tr("Enter the address of an extension index first.")); return; }
    setBusy(true);
    m_message.clear();
    emit messageChanged();
    QNetworkReply* r = net()->get(QNetworkRequest(url));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) { fail(tr("Couldn't load the extension list: %1").arg(r->errorString())); return; }
        const QJsonObject root = QJsonDocument::fromJson(r->readAll()).object();
        if (root.value("version").toInt() != 1) { fail(tr("This index isn't in a format Lumen understands.")); return; }
        m_items.clear();
        for (const QJsonValue& v : root.value("extensions").toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value("id").toString().isEmpty() || o.value("download").toObject().value("url").toString().isEmpty()) continue;
            QVariantMap m = o.toVariantMap();
            m["url"] = o.value("download").toObject().value("url").toString();
            m["sha256"] = o.value("download").toObject().value("sha256").toString().toLower();
            m_items << m;
        }
        if (m_items.isEmpty()) m_message = tr("This index has no extensions.");
        setBusy(false);
        emit messageChanged();
        emit itemsChanged();
    });
}

void Marketplace::install(const QString& id)
{
    if (m_busy) return;
    QVariantMap item;
    for (const QVariant& v : std::as_const(m_items)) if (v.toMap().value("id") == id) item = v.toMap();
    if (item.isEmpty()) return;
    if (item.value("sha256").toString().size() != 64) { fail(tr("This listing has no checksum, so Lumen won't install it.")); return; }

    QUrl url(item.value("url").toString());
    if (url.isRelative()) url = QUrl(m_url.startsWith('/') ? QUrl::fromLocalFile(m_url) : QUrl(m_url)).resolved(url);
    setBusy(true);
    m_message = tr("Downloading %1\u2026").arg(item.value("name").toString());
    emit messageChanged();
    QNetworkReply* r = net()->get(QNetworkRequest(url));
    connect(r, &QNetworkReply::finished, this, [this, r, item] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) { fail(tr("Download failed: %1").arg(r->errorString())); return; }
        const QByteArray data = r->readAll();
        const QString got = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        if (got != item.value("sha256").toString()) {
            fail(tr("The download doesn't match its checksum, so it was not installed."));
            return;
        }
        QString err;
        if (!unpack(data, item, &err)) { fail(err); return; }
        m_plugins->rescan();
        m_message = tr("%1 installed. Turn it on in the list above when you're ready.").arg(item.value("name").toString());
        emit messageChanged();
        setBusy(false);
    });
}

bool Marketplace::unpack(const QByteArray& data, const QVariantMap& item, QString* error)
{
    const QString id = item.value("id").toString();
    // the id becomes a folder name: keep it boring
    static const QRegularExpression ok(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
    if (!ok.match(id).hasMatch()) { *error = tr("That extension has an unsafe id."); return false; }

    QTemporaryDir tmp;
    if (!tmp.isValid()) { *error = tr("Couldn't create a temporary folder."); return false; }
    const QString archive = tmp.filePath("pkg.tar.gz");
    QFile f(archive);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) { *error = tr("Couldn't write the download."); return false; }
    f.close();
    const QString out = tmp.filePath("out");
    QDir().mkpath(out);

    QProcess tar;
    tar.start("tar", {"-xzf", archive, "-C", out, "--no-same-owner", "--no-same-permissions"});
    if (!tar.waitForFinished(30000) || tar.exitCode() != 0) { *error = tr("Couldn't unpack the extension (is it a .tar.gz?)."); return false; }

    // Locate plugin.json (root or a single top-level folder) and refuse links.
    QString root = out;
    if (!QFileInfo::exists(out + "/plugin.json")) {
        const QStringList subs = QDir(out).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (subs.size() == 1 && QFileInfo::exists(out + "/" + subs[0] + "/plugin.json")) root = out + "/" + subs[0];
        else { *error = tr("The package has no plugin.json."); return false; }
    }
    for (QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden, QDirIterator::Subdirectories); it.hasNext();) {
        it.next();
        if (it.fileInfo().isSymLink()) { *error = tr("The package contains a link, so it was rejected."); return false; }
    }
    QFile manifest(root + "/plugin.json");
    manifest.open(QIODevice::ReadOnly);
    const QJsonObject m = QJsonDocument::fromJson(manifest.readAll()).object();
    if (m.value("id").toString() != id) { *error = tr("The package's id doesn't match its listing."); return false; }

    const QString dest = QDir(m_plugins->userDir()).filePath(id);
    if (QFileInfo::exists(dest) && !QDir(dest).removeRecursively()) { *error = tr("Couldn't replace the old version."); return false; }
    // QDir::rename can't cross filesystems (/tmp -> home): copy then.
    std::function<bool(const QString&, const QString&)> copyTree = [&](const QString& from, const QString& to) {
        QDir().mkpath(to);
        const QDir d(from);
        for (const QFileInfo& e : d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (e.isDir()) { if (!copyTree(e.absoluteFilePath(), to + "/" + e.fileName())) return false; }
            else if (!QFile::copy(e.absoluteFilePath(), to + "/" + e.fileName())) return false;
            if (e.isFile()) QFile::setPermissions(to + "/" + e.fileName(), e.permissions() & ~(QFileDevice::WriteGroup | QFileDevice::WriteOther));
        }
        return true;
    };
    if (!copyTree(root, dest)) { *error = tr("Couldn't copy the extension into place."); return false; }
    return true;
}
