// The grounds the music is painted on, generated rather than shipped so they
// are sharp at any window size and scale.
#pragma once

#include <QColor>
#include <QImage>
#include <QSize>

namespace paper {

// Raw (unsized) xuan, the absorbent paper calligraphers use: warm, mottled,
// with long fibres and the faint laid lines a bamboo screen leaves. `scale`
// is device pixels per logical pixel.
QImage sheet(QSize size, qreal scale, quint32 seed = 7);

// The dark ground the Projection mode glows on.
QImage night(QSize size, qreal scale, quint32 seed = 7);

QColor paperColor();
QColor nightColor();
QColor inkColor();

} // namespace paper
