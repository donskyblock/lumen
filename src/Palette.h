#pragma once
#include <QColor>
#include <QObject>

// Material-3-style tonal palette generated from the dominant hue of the cover
// art. Exposed to QML as `Pal`. Colours are tone-mapped from one seed hue, so
// the whole UI re-tints when the track changes.
class Palette : public QObject {
    Q_OBJECT
#define PAL(name) \
    Q_PROPERTY(QColor name READ name NOTIFY changed) \
public: QColor name() const { return m_##name; } \
private: QColor m_##name;
    PAL(surface)            // window background
    PAL(surfaceLow)         // panels
    PAL(surfaceHigh)        // inputs, chips
    PAL(surfaceHighest)     // hover / pressed layers
    PAL(primary)
    PAL(onPrimary)
    PAL(primaryContainer)
    PAL(onPrimaryContainer)
    PAL(secondaryContainer)
    PAL(onSecondaryContainer)
    PAL(tertiary)           // lossless badge
    PAL(onTertiary)
    PAL(onSurface)
    PAL(onSurfaceVariant)
    PAL(outline)
    PAL(outlineVariant)
#undef PAL
public:
    explicit Palette(QObject* parent = nullptr);

public slots:
    void updateFromCover(const QString& trackPath);   // async
    void reset();

signals:
    void changed();

private:
    void apply(double hueDeg, double chroma);
    int m_gen = 0;
};
