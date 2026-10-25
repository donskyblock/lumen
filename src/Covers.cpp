#include "Covers.h"
#include <QCache>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>

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

QImage loadRaw(const QString& path)
{
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
    QImage img = Covers::render(path, parts[1].toInt(), parts[2].toInt());
    if (img.isNull()) return {};
    {
        QMutexLocker l(&g_mx);
        g_cache.insert(id, new QImage(img), int(img.sizeInBytes() / 1024) + 1);
    }
    if (size) *size = img.size();
    return img;
}
