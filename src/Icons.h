#pragma once
#include <QQuickImageProvider>

// Material icons drawn from their SVG path data with QPainter, so icons are
// crisp at any DPI without MSAA, QtSvg or an icon font.
// URL: image://icon/<name>@<pixels>@<rrggbb or aarrggbb>
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
