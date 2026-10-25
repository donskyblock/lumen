#include "WavyProgress.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

WavyProgress::WavyProgress(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::Image);
    setAntialiasing(true);
    connect(this, &WavyProgress::changed, this, [this] { update(); });
}

void WavyProgress::paint(QPainter* p)
{
    p->setRenderHint(QPainter::Antialiasing);
    const qreal w = width(), h = height(), cy = h / 2;
    const qreal pad = 6;
    const qreal x0 = pad, x1 = w - pad;
    const qreal x = x0 + qBound(0.0, m_value, 1.0) * (x1 - x0);
    const qreal gap = 8;

    // inactive track
    if (x + gap < x1) {
        p->setPen(QPen(m_track, m_thickness, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(x + gap, cy), QPointF(x1, cy));
    }
    // active wave
    QPainterPath wave;
    const qreal end = qMax(x0, x - gap / 2);
    const qreal k = 2 * M_PI / m_wavelength;
    wave.moveTo(x0, cy + m_amp * std::sin(-m_phase));
    for (qreal px = x0 + 1.5; px < end; px += 1.5) wave.lineTo(px, cy + m_amp * std::sin(k * (px - x0) - m_phase));
    wave.lineTo(end, cy + m_amp * std::sin(k * (end - x0) - m_phase));
    p->setPen(QPen(m_active, m_thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(wave);

    // handle: a tall pill, like the M3 expressive slider
    p->setPen(Qt::NoPen);
    p->setBrush(m_active);
    p->drawRoundedRect(QRectF(x - 2, cy - 11, 4, 22), 2, 2);
}
