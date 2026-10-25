#pragma once
#include <QImage>
#include <QString>
#include <QQuickImageProvider>

namespace Covers {
// Cover URLs are "image://cover/<key>@<size>@<radius%>" where <key> is the
// base64url of the audio file path. No lookup table is needed anywhere.
QString key(const QString& path);
QString pathFromKey(const QString& key);

// Embedded art (via FFmpeg) or cover.jpg / folder.png next to the file.
QImage loadRaw(const QString& path);

// Centre-cropped square, scaled to `size`, with rounded corners baked in so
// QML needs no masks or offscreen layers (cheaper on RAM).
QImage render(const QString& path, int size, int radiusPercent);
}

class CoverProvider : public QQuickImageProvider {
public:
    CoverProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
