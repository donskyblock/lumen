#pragma once
#include <QColor>
#include <QObject>
#include <QVariantMap>

// Theme exposed to QML as `Pal`: Material-3-style tonal colours generated from
// one seed hue (the cover art, or a fixed accent), plus the look settings of
// the active profile (corner radius, density, font scale, motion, backdrop).
class Palette : public QObject {
    Q_OBJECT
#define PAL(name) \
    Q_PROPERTY(QColor name READ name NOTIFY changed) \
public: QColor name() const { return m_##name; } \
private: QColor m_##name;
    PAL(surface) PAL(surfaceLow) PAL(surfaceHigh) PAL(surfaceHighest)
    PAL(primary) PAL(onPrimary) PAL(primaryContainer) PAL(onPrimaryContainer)
    PAL(secondaryContainer) PAL(onSecondaryContainer)
    PAL(tertiary) PAL(onTertiary)
    PAL(onSurface) PAL(onSurfaceVariant) PAL(outline) PAL(outlineVariant)
    PAL(error)
#undef PAL
    Q_PROPERTY(double rs READ rs NOTIFY changed)            // corner radius scale
    Q_PROPERTY(double pf READ pf NOTIFY changed)            // pill factor (0.5 = fully round ends)
    Q_PROPERTY(double dens READ dens NOTIFY changed)        // row height scale
    Q_PROPERTY(double fs READ fs NOTIFY changed)            // font scale
    Q_PROPERTY(bool motion READ motion NOTIFY changed)
    Q_PROPERTY(bool wavy READ wavy NOTIFY changed)
    Q_PROPERTY(double backdrop READ backdrop NOTIFY changed)
    Q_PROPERTY(int cardW READ cardW NOTIFY changed)
    Q_PROPERTY(bool isLight READ isLight NOTIFY changed)
public:
    explicit Palette(QObject* parent = nullptr);
    double rs() const { return m_rs; }
    double pf() const { return m_pf; }
    double dens() const { return m_dens; }
    double fs() const { return m_fs; }
    bool motion() const { return m_motion; }
    bool wavy() const { return m_wavy; }
    double backdrop() const { return m_backdrop; }
    int cardW() const { return m_cardW; }
    bool isLight() const { return m_scheme == QLatin1String("light"); }

public slots:
    void updateFromCover(const QString& trackPath);   // async; ignored when the accent is fixed
    void reset();
    void applyProfile(const QVariantMap& profile);

signals:
    void changed();

private:
    void apply();
    double m_rs = 1, m_pf = 0.5, m_dens = 1, m_fs = 1, m_backdrop = 0.6;
    bool m_motion = true, m_wavy = true;
    int m_cardW = 190;
    QString m_scheme = QStringLiteral("dark");
    bool m_fixed = false;
    double m_fixedHue = 208, m_fixedChroma = 0.6;
    double m_hue = 208, m_chroma = 0.6;            // current seed
    int m_gen = 0;
};
