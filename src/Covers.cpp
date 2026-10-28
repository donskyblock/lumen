#include "Covers.h"
#include "Db.h"
#include <QCryptographicHash>
#include <QThread>
#include <future>
#include <memory>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QCache>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
}

namespace Covers {

QString key(const QString& path)
{
    return QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString pathFromKey(const QString& k)
{
    return QString::fromUtf8(QByteArray::fromBase64(k.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// Art for online songs: downloaded once, kept on disk, then handled exactly like a local image
// (so it gets the same cropping and rounded corners). The image provider runs on pool threads, so all
// network traffic goes through one dedicated thread that owns the QNetworkAccessManager.
namespace {
class Fetcher {
public:
    static Fetcher& get() { static Fetcher f; return f; }
    QByteArray fetch(const QString& url)
    {
        auto prom = std::make_shared<std::promise<QByteArray>>();
        std::future<QByteArray> fut = prom->get_future();
        QMetaObject::invokeMethod(m_ctx, [this, url, prom] {
            if (!m_nam) m_nam = new QNetworkAccessManager(m_ctx);
            QNetworkRequest req{QUrl(url)};
            req.setTransferTimeout(10000);
            QNetworkReply* r = m_nam->get(req);
            QObject::connect(r, &QNetworkReply::finished, m_ctx, [r, prom] {
                prom->set_value(r->error() == QNetworkReply::NoError ? r->readAll() : QByteArray());
                r->deleteLater();
            });
        }, Qt::QueuedConnection);
        if (fut.wait_for(std::chrono::seconds(15)) != std::future_status::ready) return {};
        return fut.get();
    }
private:
    Fetcher()
    {
        m_thread.setObjectName(QStringLiteral("lumen-covers"));
        m_ctx = new QObject;
        m_ctx->moveToThread(&m_thread);
        m_thread.start();
    }
    ~Fetcher()
    {
        QMetaObject::invokeMethod(m_ctx, [this] { delete m_nam; m_nam = nullptr; }, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
        delete m_ctx;
    }
    QThread m_thread;
    QObject* m_ctx = nullptr;
    QNetworkAccessManager* m_nam = nullptr;      // created and used on m_thread only
};
}

static QImage loadRemote(const QString& url)
{
    const QDir dir(QDir(Db::get().dataDir()).filePath("covercache"));
    QDir().mkpath(dir.path());
    const QString file = dir.filePath(QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex()));
    QImage img(file);
    if (!img.isNull()) return img;
    const QByteArray body = Fetcher::get().fetch(url);
    img = QImage::fromData(body);
    if (!img.isNull()) { QFile f(file); if (f.open(QIODevice::WriteOnly)) f.write(body); }
    return img;
}

QImage loadRaw(const QString& path)
{
    if (path.startsWith(QLatin1String("http://")) || path.startsWith(QLatin1String("https://"))) return loadRemote(path);
    AVFormatContext* fmt = nullptr;
    const QByteArray p = path.toUtf8();
    if (avformat_open_input(&fmt, p.constData(), nullptr, nullptr) >= 0) {
        QImage img;
        for (unsigned i = 0; i < fmt->nb_streams && img.isNull(); ++i) {
            AVStream* st = fmt->streams[i];
            if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
                img = QImage::fromData(st->attached_pic.data, st->attached_pic.size);
        }
        avformat_close_input(&fmt);
        if (!img.isNull()) return img;
    }
    const QDir dir = QFileInfo(path).dir();
    const QStringList names = dir.entryList({"cover.*", "folder.*", "front.*", "album.*"}, QDir::Files);
    for (const QString& n : names) {
        const QString ext = QFileInfo(n).suffix().toLower();
        if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp") {
            QImage img(dir.filePath(n));
            if (!img.isNull()) return img;
        }
    }
    return {};
}

QImage render(const QString& path, int size, int radiusPercent)
{
    QImage src = loadRaw(path);
    if (src.isNull() || size < 8) return {};
    const int side = qMin(src.width(), src.height());
    src = src.copy((src.width() - side) / 2, (src.height() - side) / 2, side, side)
              .scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (radiusPercent <= 0) return src.convertToFormat(QImage::Format_RGB32);

    QImage out(size, size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath rr;
    const qreal r = size * radiusPercent / 100.0;
    rr.addRoundedRect(QRectF(0, 0, size, size), r, r);
    p.setClipPath(rr);
    p.drawImage(0, 0, src);
    return out;
}

} // namespace Covers

// ---------------------------------------------------------------------------

namespace {
QMutex g_mx;
QCache<QString, QImage> g_cache(24 * 1024);   // cost = KiB, so ~24 MB ceiling

// Missing embedded/sidecar art is normal for loose audio files. Return a
// proper image rather than an empty QImage: Qt otherwise logs one provider
// error for every card that happens to show the track.
QImage missingCover(int size, int radiusPercent)
{
    size = std::max(8, size);
    QImage out(size, size, QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor("#303038"));
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath rr;
    const qreal r = size * std::clamp(radiusPercent, 0, 50) / 100.0;
    rr.addRoundedRect(QRectF(0, 0, size, size), r, r);
    p.setClipPath(rr);
    p.fillRect(out.rect(), QColor("#303038"));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#bfc0ca"));
    const qreal s = size;
    p.drawEllipse(QRectF(s * .27, s * .61, s * .22, s * .17));
    p.drawRect(QRectF(s * .46, s * .28, s * .075, s * .42));
    p.drawRect(QRectF(s * .49, s * .28, s * .25, s * .075));
    p.drawEllipse(QRectF(s * .58, s * .50, s * .22, s * .17));
    return out;
}
}

CoverProvider::CoverProvider()
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading) {}

QImage CoverProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    {
        QMutexLocker l(&g_mx);
        if (QImage* hit = g_cache.object(id)) {
            if (size) *size = hit->size();
            return *hit;
        }
    }
    const QStringList parts = id.split('@');
    if (parts.size() < 3) return {};
    const QString path = Covers::pathFromKey(parts[0]);
    const int edge = parts[1].toInt();
    const int radius = parts[2].toInt();
    QImage img = Covers::render(path, edge, radius);
    if (img.isNull()) img = missingCover(edge, radius);
    {
        QMutexLocker l(&g_mx);
        g_cache.insert(id, new QImage(img), int(img.sizeInBytes() / 1024) + 1);
    }
    if (size) *size = img.size();
    return img;
}
