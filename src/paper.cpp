#include "paper.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace {

// Smooth blotches: a tiny random image scaled up with bilinear filtering is
// value noise for free, and far faster than evaluating noise per pixel.
QImage mottle(QSize size, int cell, QRandomGenerator &random)
{
    QImage small(std::max(2, size.width() / cell + 2), std::max(2, size.height() / cell + 2), QImage::Format_Grayscale8);
    for (int y = 0; y < small.height(); ++y) {
        uchar *row = small.scanLine(y);
        for (int x = 0; x < small.width(); ++x)
            row[x] = uchar(random.bounded(256));
    }
    return small.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void grain(QImage &image, QRandomGenerator &random, int amplitude)
{
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int d = int(random.bounded(2 * amplitude + 1)) - amplitude;
            const QRgb c = row[x];
            row[x] = qRgb(std::clamp(qRed(c) + d, 0, 255), std::clamp(qGreen(c) + d, 0, 255),
                          std::clamp(qBlue(c) + d, 0, 255));
        }
    }
}

void applyMottle(QImage &image, const QImage &noise, qreal strength)
{
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        const uchar *n = noise.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int d = int((n[x] - 128) * strength);
            const QRgb c = row[x];
            row[x] = qRgb(std::clamp(qRed(c) + d, 0, 255), std::clamp(qGreen(c) + d, 0, 255),
                          std::clamp(qBlue(c) + d - d / 4, 0, 255));
        }
    }
}

void vignette(QImage &image, QColor edge)
{
    QPainter p(&image);
    const QPointF centre(image.width() / 2.0, image.height() / 2.0);
    QRadialGradient gradient(centre, std::hypot(centre.x(), centre.y()));
    QColor clear = edge;
    clear.setAlpha(0);
    gradient.setColorAt(0.0, clear);
    gradient.setColorAt(0.62, clear);
    gradient.setColorAt(1.0, edge);
    p.fillRect(image.rect(), gradient);
}

} // namespace

namespace paper {

QColor paperColor() { return QColor(0xF1, 0xEB, 0xDE); }
QColor nightColor() { return QColor(0x0B, 0x0C, 0x10); }
QColor inkColor() { return QColor(0x17, 0x15, 0x14); }

QImage sheet(QSize size, qreal scale, quint32 seed)
{
    QRandomGenerator random(seed);
    QImage image(size, QImage::Format_RGB32);
    image.fill(paperColor());

    applyMottle(image, mottle(size, int(220 * scale), random), 0.035);
    applyMottle(image, mottle(size, int(48 * scale), random), 0.02);

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    // Laid lines from the papermaker's bamboo screen, barely there.
    p.setPen(QPen(QColor(120, 100, 70, 7), std::max(1.0, scale)));
    for (qreal y = 0; y < size.height(); y += 9 * scale)
        p.drawLine(QPointF(0, y), QPointF(size.width(), y));
    p.setPen(QPen(QColor(120, 100, 70, 6), 1.5 * scale));
    for (qreal x = 40 * scale; x < size.width(); x += 132 * scale)
        p.drawLine(QPointF(x, 0), QPointF(x, size.height()));

    // Fibres: short curved strands, a few darker, most lighter than the sheet.
    const int fibres = int(qreal(size.width()) * size.height() / (900.0 * scale * scale));
    for (int i = 0; i < fibres; ++i) {
        const QPointF start(random.bounded(size.width()), random.bounded(size.height()));
        const qreal length = (8 + random.bounded(46)) * scale;
        const qreal angle = random.bounded(2.0 * M_PI);
        const QPointF dir(std::cos(angle), std::sin(angle));
        const QPointF normal(-dir.y(), dir.x());
        const qreal bend = (random.bounded(2.0) - 1.0) * length * 0.2;
        QPainterPath fibre(start);
        fibre.quadTo(start + dir * length * 0.5 + normal * bend, start + dir * length);
        const bool dark = random.bounded(9) == 0;
        p.setPen(QPen(dark ? QColor(90, 72, 50, 8 + random.bounded(10)) : QColor(255, 253, 246, 34 + random.bounded(36)),
                      (0.4 + random.bounded(0.6)) * scale, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(fibre);
    }
    p.end();

    grain(image, random, 3);
    vignette(image, QColor(110, 86, 52, 30));
    return image;
}

QImage night(QSize size, qreal scale, quint32 seed)
{
    QRandomGenerator random(seed);
    QImage image(size, QImage::Format_RGB32);
    image.fill(nightColor());
    applyMottle(image, mottle(size, int(260 * scale), random), 0.02);
    grain(image, random, 2);
    vignette(image, QColor(0, 0, 0, 120));
    return image;
}

} // namespace paper
