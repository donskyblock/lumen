#include "Palette.h"
#include "Covers.h"
#include <QtConcurrent>
#include <array>
#include <cmath>

namespace {
constexpr double kDefaultHue = 208.0;   // cool blue, close to a #1e2327 base
constexpr double kDefaultChroma = 0.6;

QColor hsl(double h, double s, double l)
{
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
    return QColor::fromHslF(float(h / 360.0), float(qBound(0.0, s, 1.0)), float(qBound(0.0, l, 1.0)));
}

// Pick the hue bucket with the most "colourful" weight.
std::pair<double, double> seedFromImage(const QImage& src)
{
    const QImage img = src.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation)
                           .convertToFormat(QImage::Format_RGB32);
    std::array<double, 24> weight{};
    std::array<double, 24> sat{};
    double total = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QColor c(row[x]);
            const double s = c.hsvSaturationF(), v = c.valueF();
            if (v < 0.12) continue;
            const double w = s * s * (0.35 + v);
            const int b = qBound(0, int(c.hsvHueF() * 24.0), 23);
            if (c.hsvHue() < 0) continue;   // grey
            weight[b] += w; sat[b] += s * w; total += w;
        }
    }
    int best = -1; double bw = 0;
    for (int i = 0; i < 24; ++i) if (weight[i] > bw) { bw = weight[i]; best = i; }
    if (best < 0 || total < 1.0) return {kDefaultHue, 0.12};   // greyscale art -> near-neutral UI
    const double chroma = qBound(0.18, sat[best] / weight[best], 0.9);
    return {(best + 0.5) * 15.0, chroma};
}
} // namespace

Palette::Palette(QObject* parent) : QObject(parent) { apply(kDefaultHue, kDefaultChroma); }

void Palette::reset() { ++m_gen; apply(kDefaultHue, kDefaultChroma); }

void Palette::updateFromCover(const QString& path)
{
    const int gen = ++m_gen;
    (void)QtConcurrent::run([this, path, gen] {
        const QImage img = Covers::loadRaw(path);
        const auto seed = img.isNull() ? std::pair<double, double>{kDefaultHue, kDefaultChroma} : seedFromImage(img);
        QMetaObject::invokeMethod(this, [this, seed, gen] {
            if (gen == m_gen) apply(seed.first, seed.second);
        }, Qt::QueuedConnection);
    });
}

void Palette::apply(double h, double c)
{
    const double ss = 0.05 + 0.11 * c;     // surface saturation
    const double ps = 0.38 + 0.42 * c;     // accent saturation
    m_surface              = hsl(h, ss, 0.085);
    m_surfaceLow           = hsl(h, ss, 0.125);
    m_surfaceHigh          = hsl(h, ss, 0.165);
    m_surfaceHighest       = hsl(h, ss, 0.215);
    m_primary              = hsl(h, ps, 0.80);
    m_onPrimary            = hsl(h, ps, 0.16);
    m_primaryContainer     = hsl(h, ps * 0.75, 0.30);
    m_onPrimaryContainer   = hsl(h, ps, 0.90);
    m_secondaryContainer   = hsl(h, ss * 1.4, 0.26);
    m_onSecondaryContainer = hsl(h, ps * 0.6, 0.90);
    m_tertiary             = hsl(168, 0.62, 0.78);
    m_onTertiary           = hsl(168, 0.60, 0.12);
    m_onSurface            = hsl(h, 0.10, 0.91);
    m_onSurfaceVariant     = hsl(h, 0.08, 0.73);
    m_outline              = hsl(h, 0.06, 0.52);
    m_outlineVariant       = hsl(h, ss, 0.30);
    emit changed();
}
