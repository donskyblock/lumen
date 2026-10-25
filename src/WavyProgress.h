#pragma once
#include <QColor>
#include <QQuickPaintedItem>

// Material 3 Expressive style "wavy" progress bar. Painted with QPainter into
// a small image (a few tens of KB), so no shaders, MSAA or offscreen layers.
class WavyProgress : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(qreal value MEMBER m_value NOTIFY changed)
    Q_PROPERTY(qreal amplitude MEMBER m_amp NOTIFY changed)
    Q_PROPERTY(qreal phase MEMBER m_phase NOTIFY changed)
    Q_PROPERTY(qreal wavelength MEMBER m_wavelength NOTIFY changed)
    Q_PROPERTY(qreal thickness MEMBER m_thickness NOTIFY changed)
    Q_PROPERTY(QColor activeColor MEMBER m_active NOTIFY changed)
    Q_PROPERTY(QColor trackColor MEMBER m_track NOTIFY changed)
public:
    explicit WavyProgress(QQuickItem* parent = nullptr);
    void paint(QPainter* p) override;
signals:
    void changed();
private:
    qreal m_value = 0, m_amp = 0, m_phase = 0, m_wavelength = 26, m_thickness = 4;
    QColor m_active = Qt::white, m_track = Qt::gray;
};
