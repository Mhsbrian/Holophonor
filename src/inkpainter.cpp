#include "inkpainter.h"

#include <QFont>
#include <QLineF>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "paper.h"

namespace {

constexpr qreal kLowPitch = 45.0; // A2 sits on the bottom margin...
constexpr qreal kPitchSpan = 48.0; // ...and A6 on the top one.

qreal smoothstep(qreal edge0, qreal edge1, qreal x)
{
    const qreal t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A round brush seen from below: a ragged edge, and bristles that run along
// the stroke (the tip is drawn rotated to the direction of travel). The dry
// tip drops most bristles, which is how a brush low on ink drags: kasure.
QImage brushTip(bool dry, quint32 seed)
{
    constexpr int size = 128;
    QRandomGenerator random(seed);
    double bristles[size];
    const double a = random.generateDouble() * 6.28, b = random.generateDouble() * 6.28;
    for (int y = 0; y < size; ++y) {
        const double smooth = 0.5 + 0.25 * std::sin(y * 0.41 + a) + 0.25 * std::sin(y * 0.97 + b);
        const double v = 0.6 * smooth + 0.4 * random.generateDouble();
        bristles[y] = dry ? (v > 0.52 ? 1.0 : 0.06) : 0.5 + 0.5 * v;
    }
    const double e1 = random.generateDouble() * 6.28, e2 = random.generateDouble() * 6.28;

    const QColor ink = paper::inkColor();
    QImage tip(size, size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size; ++y) {
        auto *row = reinterpret_cast<QRgb *>(tip.scanLine(y));
        for (int x = 0; x < size; ++x) {
            const double dx = (x - size / 2.0 + 0.5) / (size / 2.0);
            const double dy = (y - size / 2.0 + 0.5) / (size / 2.0);
            const double r = std::hypot(dx, dy);
            const double angle = std::atan2(dy, dx);
            const double edge = 0.86 + 0.07 * std::sin(angle * 5 + e1) + 0.05 * std::sin(angle * 13 + e2);
            // A wet brush is solid at its heart; bristles only show at the rim.
            const double rim = dry ? 1.0 : smoothstep(0.35, 0.9, r);
            const double alpha = (1.0 - smoothstep(edge - 0.12, edge, r)) * (1.0 - rim + rim * bristles[y]);
            const int a8 = std::clamp(int(alpha * 255.0), 0, 255);
            row[x] = qRgba(ink.red() * a8 / 255, ink.green() * a8 / 255, ink.blue() * a8 / 255, a8);
        }
    }
    return tip;
}

QFont monoFont(qreal pixelSize)
{
    QFont font;
    font.setFamilies({QStringLiteral("iA Writer Mono S"), QStringLiteral("JetBrainsMono Nerd Font"),
                      QStringLiteral("monospace")});
    font.setPixelSize(std::max(1, int(pixelSize)));
    return font;
}

// Simple ratios for each interval above the phrase's first note: the shapes
// a harmonograph draws when two pendulums swing in those proportions.
constexpr int kRatios[12][2] = {{1, 1}, {16, 15}, {9, 8}, {6, 5}, {5, 4}, {4, 3},
                                {7, 5}, {3, 2},   {8, 5}, {5, 3}, {9, 5}, {15, 8}};

} // namespace

InkPainter::InkPainter()
{
    m_tip = brushTip(false, 3);
    m_dryTip = brushTip(true, 5);
    for (const char *hex : {"#b8442e", "#c18a2a", "#cfa53a", "#6b8a45", "#4f8f80", "#34507e", "#a8506e", "#6f4e33"})
        m_pigments.append(QColor(hex));
    setPigments(m_pigments);
}

QString InkPainter::noteName(int note)
{
    static const char *names[12] = {"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};
    if (note < 0)
        return {};
    return QString::fromUtf8(names[note % 12]) + QString::number(note / 12 - 1);
}

qreal InkPainter::pitchPosition(qreal pitch) { return std::clamp((pitch - kLowPitch) / kPitchSpan, 0.0, 1.0); }

qreal InkPainter::yFor(qreal pitch) const
{
    const qreal h = m_canvas.height();
    return h * (0.86 - 0.72 * pitchPosition(pitch));
}

void InkPainter::setPigments(const QVector<QColor> &pigments)
{
    if (pigments.isEmpty())
        return;
    // Ordered around the hue wheel so neighbouring keys get neighbouring
    // colours, and deepened: theme colours are tuned for dark screens, but a
    // glaze on paper needs body to show.
    QVector<QColor> ring;
    for (QColor c : pigments) {
        float h, s, l, a;
        c.getHslF(&h, &s, &l, &a);
        c.setHslF(std::max(h, 0.0f), std::max(s, 0.42f), std::min(l, 0.52f), 1.0f);
        ring.append(c);
    }
    std::sort(ring.begin(), ring.end(), [](const QColor &a, const QColor &b) { return a.hslHueF() < b.hslHueF(); });
    m_pigments = ring;
}

QColor InkPainter::pigmentFor(qreal turn) const
{
    const int n = int(m_pigments.size());
    const qreal position = std::fmod(std::max(turn, 0.0), 1.0) * n;
    const int i = int(position) % n;
    const qreal f = position - std::floor(position);
    const QColor a = m_pigments[i], b = m_pigments[(i + 1) % n];
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f), float(a.greenF() + (b.greenF() - a.greenF()) * f),
                            float(a.blueF() + (b.blueF() - a.blueF()) * f));
}

qreal InkPainter::gaussian()
{
    const qreal u = std::max(1e-9, noise()), v = noise();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * M_PI * v);
}

// Splits every edge and pushes the midpoint out or in: three passes turn a
// decagon into the irregular rim of a watercolour pool.
QPolygonF InkPainter::deform(const QPolygonF &shape, qreal variance)
{
    QPolygonF out;
    out.reserve(shape.size() * 2);
    for (int i = 0; i < shape.size(); ++i) {
        const QPointF a = shape[i], b = shape[(i + 1) % shape.size()];
        const QPointF edge = b - a;
        const QPointF normal(-edge.y(), edge.x());
        out << a << (a + b) / 2.0 + normal * gaussian() * variance;
    }
    return out;
}

void InkPainter::resize(QSize pixels, qreal scale)
{
    if (pixels.isEmpty() || (pixels == m_canvas.size() && qFuzzyCompare(scale, m_scale)))
        return;
    const QSize old = m_canvas.size();
    m_scale = scale;
    m_paper = paper::sheet(pixels, scale);
    m_night = paper::night(pixels, scale);
    if (m_canvas.isNull()) {
        m_canvas = ground().copy();
        return;
    }
    // Keep the painting across window resizes; tiling moves windows often.
    m_canvas = m_canvas.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGB32);
    const qreal sx = qreal(pixels.width()) / old.width(), sy = qreal(pixels.height()) / old.height();
    m_x *= sx;
    m_prev = QPointF(m_prev.x() * sx, m_prev.y() * sy);
    m_bloomCentre = QPointF(m_bloomCentre.x() * sx, m_bloomCentre.y() * sy);
    m_lit = m_canvas.rect();
}

void InkPainter::resetPen()
{
    m_x = 0.0;
    m_stroke = m_sheetInk = m_sealed = false;
    m_inkLoad = 1.0;
    m_carry = m_lastHatch = 0.0;
    m_blooming = m_hasBloom = false;
    m_bloomNote = -1;
    m_blooms = 0;
    m_gridDrawn = false;
    m_tonic = -1;
    m_glow = 0.0;
    m_sparks.clear();
    m_lit = QRect();
}

void InkPainter::setMode(int mode)
{
    mode = std::clamp(mode, 0, ModeCount - 1);
    if (mode == m_mode)
        return;
    m_mode = mode;
    m_transition = 0.55;
    m_absorb = 0.0;
    resetPen();
}

void InkPainter::freshSheet()
{
    m_transition = 0.55;
    m_absorb = 0.0;
    resetPen();
}

void InkPainter::blendGround(qreal opacity)
{
    QPainter p(&m_canvas);
    p.setOpacity(std::clamp(opacity, 0.0, 1.0));
    p.drawImage(0, 0, ground());
}

// Moves every pixel in `area` a fraction of the way back to the ground,
// keeping `keep`/256 of the difference. Rounding toward the ground means it
// always arrives; an 8-bit alpha blend stalls a few levels short and leaves
// a ghost of whatever was there.
void InkPainter::sinkToGround(QRect area, int keep)
{
    area &= m_canvas.rect();
    keep = std::clamp(keep, 0, 255);
    const QImage &base = ground();
    auto toward = [keep](int value, int target) {
        const int d = value - target;
        return target + (d >= 0 ? (d * keep) >> 8 : -((-d * keep) >> 8));
    };
    for (int y = area.top(); y <= area.bottom(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(m_canvas.scanLine(y));
        const auto *target = reinterpret_cast<const QRgb *>(base.constScanLine(y));
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb c = row[x], t = target[x];
            if (c != t)
                row[x] = qRgb(toward(qRed(c), qRed(t)), toward(qGreen(c), qGreen(t)), toward(qBlue(c), qBlue(t)));
        }
    }
}

qreal InkPainter::brushRadius(qreal level) const
{
    return m_canvas.height() * (0.002 + 0.028 * std::pow(level, 1.3));
}

// The finished line sinks into the sheet as a ghost, and writing starts again
// at the left margin. A long performance becomes a palimpsest, never a smear.
void InkPainter::turnSheet()
{
    m_absorb = 0.9;
    m_absorbRate = 3.0;
    m_x = 0.0;
    m_stroke = m_sheetInk = m_sealed = false;
    m_gridDrawn = false;
    m_carry = m_lastHatch = 0.0;
}

bool InkPainter::step(const Synth::Snapshot &s, double dt)
{
    if (m_canvas.isNull())
        return false;
    dt = std::clamp(dt, 0.0, 0.1);
    m_time += dt;
    const bool newNote = s.serial != m_serial;
    m_serial = s.serial;
    const bool sounding = s.sounding && s.level > 0.012f;

    // A new sheet or a new mode wipes to its ground first; painting resumes
    // once the wipe lands exactly, so nothing of the old picture lingers.
    if (m_transition > 0.0) {
        m_transition -= dt;
        sinkToGround(m_canvas.rect(), m_transition > 0.0 ? int(256.0 * std::max(0.0, 1.0 - dt * 7.0)) : 0);
        m_silence = sounding ? 0.0 : m_silence + dt;
        return true;
    }
    bool changed = false;
    if (m_absorb > 0.0) {
        blendGround(dt * m_absorbRate);
        m_absorb -= dt;
        changed = true;
    }

    switch (m_mode) {
    case Sumi:
        changed |= stepSumi(s, dt, sounding);
        break;
    case Bloom:
        changed |= stepBloom(s, dt, sounding, newNote);
        break;
    case Graphite:
        changed |= stepGraphite(s, dt, sounding, newNote);
        break;
    case Projection:
        changed |= stepProjection(s, dt, sounding, newNote);
        break;
    }
    m_silence = sounding ? 0.0 : m_silence + dt;
    return changed;
}

// --- Sumi: one brush writes the melody left to right. Height is pitch,
// weight is breath; vibrato makes the line breathe, legato makes it flow.

bool InkPainter::stepSumi(const Synth::Snapshot &s, double dt, bool sounding)
{
    const qreal w = m_canvas.width();
    const qreal margin = w * 0.07;
    const qreal speed = w / 26.0;
    if (m_x < margin)
        m_x = margin;

    if (!sounding) {
        m_stroke = false;
        if (m_sheetInk && !m_sealed && m_silence > 2.5) {
            placeSeal();
            return true;
        }
        return false;
    }

    // The brush has weight: it curves into a new pitch rather than stepping.
    const qreal target = yFor(s.pitch);
    m_penY = m_stroke ? m_penY + (target - m_penY) * (1.0 - std::exp(-dt / 0.12)) : target;
    const qreal y = m_penY;
    if (!m_stroke) {
        // A rest leaves space on the sheet, as a pause would on the page.
        if (m_sheetInk)
            m_x += std::min(m_silence, 1.6) * speed * 0.55 + 5.0 * m_scale;
        if (m_x > w - margin) {
            turnSheet();
            m_x = margin;
        }
        m_stroke = true;
        m_inkLoad = 1.0;
        m_carry = 0.0;
        m_prev = QPointF(m_x, y);
        // The brush meets the paper with a little more ink than it carries.
        QPainter p(&m_canvas);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal r = brushRadius(s.level);
        stamp(p, m_prev, r * 1.1, 0.3, 0.0, false);
    }
    m_x += speed * dt;
    const QPointF to(m_x, y);
    brush(m_prev, to, s.level);
    m_prev = to;
    m_sheetInk = true;
    if (m_x > w - margin) {
        m_stroke = false;
        turnSheet();
    }
    return true;
}

void InkPainter::brush(const QPointF &from, const QPointF &to, qreal level)
{
    const QLineF segment(from, to);
    const qreal length = segment.length();
    const qreal h = m_canvas.height();
    const qreal radius = brushRadius(level);
    const qreal spacing = std::max(0.5 * m_scale, radius * 0.12);
    const qreal angle = qRadiansToDegrees(std::atan2(to.y() - from.y(), to.x() - from.x()));

    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    qreal at = spacing - m_carry;
    while (at <= length) {
        const QPointF point = length > 0.0 ? segment.pointAt(at / length) : to;
        // The brush gives up its ink as it travels; fast leaps in pitch
        // spread the same ink thinner, which is what reads as speed.
        m_inkLoad = std::max(0.0, m_inkLoad - spacing / (h * 0.7));
        const bool dry = m_inkLoad < 0.3;
        const qreal opacity = (0.08 + 0.1 * level) * (0.5 + 0.5 * m_inkLoad);
        const QPointF jitter(0.0, (noise() - 0.5) * 0.4 * m_scale);
        stamp(p, point + jitter, radius * (0.94 + 0.12 * noise()), opacity, angle, dry);
        if (!dry && noise() < 0.06)
            stamp(p, point, radius * 1.7, 0.018, angle, false); // ink bleeding into the fibres
        at += spacing;
    }
    m_carry = length - (at - spacing);
}

void InkPainter::stamp(QPainter &p, const QPointF &at, qreal radius, qreal opacity, qreal angle, bool dry)
{
    p.save();
    p.translate(at);
    p.rotate(angle);
    p.setOpacity(opacity);
    p.drawImage(QRectF(-radius, -radius, 2.0 * radius, 2.0 * radius), dry ? m_dryTip : m_tip);
    p.restore();
}

// The performer's seal, pressed once a phrase has settled: vermilion paste,
// the character for sound carved out so the paper shows through.
void InkPainter::placeSeal()
{
    const qreal w = m_canvas.width(), h = m_canvas.height();
    const qreal size = h * 0.05;
    const QPointF at(std::min(m_x + size * 0.6, w - size * 1.8),
                     std::clamp(m_prev.y() + size * 0.9, h * 0.1, h * 0.9 - size));
    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.translate(at + QPointF(size / 2.0, size / 2.0));
    p.rotate(-2.5 + noise() * 2.0);
    const QRectF square(-size / 2.0, -size / 2.0, size, size);
    p.setOpacity(0.86);
    p.setPen(Qt::NoPen);
    p.setBrush(m_vermilion);
    p.drawRoundedRect(square, size * 0.08, size * 0.08);

    QFont glyph;
    glyph.setFamilies({QStringLiteral("Noto Serif CJK JP"), QStringLiteral("Noto Serif CJK SC"),
                       QStringLiteral("serif")});
    glyph.setPixelSize(std::max(1, int(size * 0.72)));
    glyph.setWeight(QFont::Black);
    p.setFont(glyph);
    p.setPen(paper::paperColor());
    p.drawText(square, Qt::AlignCenter, QStringLiteral("音"));

    // An uneven impression: specks where the paste didn't take.
    p.setOpacity(0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(paper::paperColor());
    for (int i = 0; i < 70; ++i) {
        const QPointF speck(square.left() + noise() * size, square.top() + noise() * size);
        const qreal r = (0.3 + noise() * 0.9) * m_scale;
        p.drawEllipse(speck, r, r);
    }
    m_sealed = true;
}

// --- Bloom: every note drops pigment that spreads while it's held. The
// colour comes from the note's place on the circle of fifths, so related
// harmonies share related colours; the melody walks the brush across the sheet.

bool InkPainter::stepBloom(const Synth::Snapshot &s, double dt, bool sounding, bool newNote)
{
    const qreal w = m_canvas.width(), h = m_canvas.height();
    if (!sounding) {
        m_blooming = false;
        return false;
    }
    if (newNote || !m_blooming) {
        const int note = s.note >= 0 ? s.note : int(std::lround(s.pitch));
        const int fifths = (((note % 12) + 12) % 12) * 7 % 12;
        QPointF centre;
        if (!m_hasBloom) {
            centre = QPointF(w * (0.38 + 0.24 * noise()), h * (0.38 + 0.24 * noise()));
        } else {
            const qreal angle = fifths / 12.0 * 2.0 * M_PI + (noise() - 0.5) * 0.7;
            const qreal distance = h * (0.1 + 0.015 * std::min(12, std::abs(note - m_bloomNote)));
            centre = m_bloomCentre + QPointF(std::cos(angle), std::sin(angle)) * distance;
            // A gentle pull toward the middle keeps the walk composing a
            // picture instead of drifting into one corner.
            centre += (QPointF(w / 2.0, h / 2.0) - centre) * 0.15;
            // Reflect off the margins so the walk stays on the sheet.
            const qreal left = w * 0.1, right = w * 0.9, top = h * 0.16, bottom = h * 0.86;
            if (centre.x() < left)
                centre.setX(2 * left - centre.x());
            if (centre.x() > right)
                centre.setX(2 * right - centre.x());
            if (centre.y() < top)
                centre.setY(2 * top - centre.y());
            if (centre.y() > bottom)
                centre.setY(2 * bottom - centre.y());
        }
        m_hasBloom = true;
        m_bloomCentre = centre;
        m_bloomNote = note;
        m_bloomColour = pigmentFor(fifths / 12.0);
        m_bloomMax = h * (0.045 + 0.09 * (1.0 - pitchPosition(s.pitch)));
        m_bloomRadius = m_bloomMax * 0.15;
        m_bloomFrames = 0;
        QPolygonF shape;
        for (int i = 0; i < 10; ++i) {
            const qreal a = i / 10.0 * 2.0 * M_PI;
            shape << QPointF(std::cos(a), std::sin(a));
        }
        for (int pass = 0; pass < 3; ++pass)
            shape = deform(shape, 0.22);
        m_bloomShape = shape;
        m_blooming = true;
        // Older glazes recede now and then so the painting never turns to mud.
        if (++m_blooms % 42 == 0) {
            m_absorb = 1.2;
            m_absorbRate = 0.6;
        }
    }

    m_bloomRadius = std::min(m_bloomMax, m_bloomRadius + dt * m_bloomMax * (0.2 + 1.1 * s.level));
    QPolygonF layer = deform(m_bloomShape, 0.3);
    // Most layers reach the rim, some settle inside: a soft wash, not rings.
    const qreal scale = m_bloomRadius * (0.5 + 0.55 * std::sqrt(noise()));
    const QPointF offset((noise() - 0.5) * m_bloomRadius * 0.08, (noise() - 0.5) * m_bloomRadius * 0.08);
    for (QPointF &point : layer)
        point = m_bloomCentre + offset + point * scale;

    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Multiply);
    // Pigment spreads fast while the pool is young and barely deepens once it
    // has reached its size, so a long note stays luminous instead of going black.
    const qreal young = 1.0 - m_bloomRadius / m_bloomMax;
    QColor glaze = m_bloomColour;
    glaze.setAlphaF(float((0.014 + 0.03 * s.level) * (0.25 + 0.75 * young)));
    p.setPen(Qt::NoPen);
    p.setBrush(glaze);
    p.drawPolygon(layer);
    // Pigment collects at the drying edge: watercolour's darker rim.
    if (m_bloomFrames % 3 == 0 && young > 0.05) {
        QColor rim = m_bloomColour.darker(135);
        rim.setAlphaF(0.06f);
        p.setPen(QPen(rim, 1.1 * m_scale));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(layer);
    }
    // Granulation: heavier pigment settling into the paper's tooth.
    QColor grain = m_bloomColour.darker(150);
    grain.setAlphaF(0.08f);
    p.setPen(Qt::NoPen);
    p.setBrush(grain);
    for (int i = 0; i < 4; ++i) {
        const qreal a = noise() * 2.0 * M_PI, r = std::sqrt(noise()) * m_bloomRadius * 0.85;
        const qreal dot = (0.5 + noise()) * m_scale;
        p.drawEllipse(m_bloomCentre + QPointF(std::cos(a), std::sin(a)) * r, dot, dot);
    }
    ++m_bloomFrames;
    return true;
}

// --- Graphite: a draughtsman's score. A fine line for pitch, hatching for
// breath, every note named in small type; octaves ruled on the sheet.

void InkPainter::drawGrid()
{
    const qreal w = m_canvas.width();
    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(monoFont(10.0 * m_scale));
    for (int note = 48; note <= 84; note += 12) {
        const qreal y = yFor(note);
        p.setPen(QPen(QColor(40, 40, 46, 26), 1.0 * m_scale));
        p.drawLine(QPointF(w * 0.06, y), QPointF(w * 0.97, y));
        p.setPen(QColor(40, 40, 46, 90));
        p.drawText(QPointF(w * 0.025, y + 3.5 * m_scale), noteName(note));
    }
}

bool InkPainter::stepGraphite(const Synth::Snapshot &s, double dt, bool sounding, bool newNote)
{
    const qreal w = m_canvas.width(), h = m_canvas.height();
    const qreal margin = w * 0.08;
    bool changed = false;
    if (!m_gridDrawn && m_absorb <= 0.0 && m_transition <= 0.0) {
        drawGrid();
        m_gridDrawn = true;
        changed = true;
    }
    if (m_x < margin)
        m_x = margin;
    // Time keeps moving through short rests, so rhythm shows as space.
    if (m_sheetInk && (sounding || m_silence < 1.2))
        m_x += w / 30.0 * dt;
    if (!sounding) {
        m_stroke = false;
        return changed;
    }

    const QPointF to(m_x, yFor(s.pitch));
    const QColor graphite(44, 44, 50);
    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    if (m_stroke) {
        QColor line = graphite;
        line.setAlphaF(0.85f);
        p.setPen(QPen(line, 1.25 * m_scale, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(m_prev, to);
    }
    if (m_x - m_lastHatch >= 3.0 * m_scale) {
        QColor hatch = graphite;
        hatch.setAlphaF(0.16f);
        p.setPen(QPen(hatch, 0.8 * m_scale));
        p.drawLine(QPointF(m_x, to.y() + 3.0 * m_scale), QPointF(m_x, to.y() + 3.0 * m_scale + s.level * h * 0.07));
        m_lastHatch = m_x;
    }
    if (newNote || !m_stroke) {
        QColor mark = graphite;
        mark.setAlphaF(0.6f);
        p.setPen(Qt::NoPen);
        p.setBrush(mark);
        p.drawEllipse(to, 1.8 * m_scale, 1.8 * m_scale);
        p.setPen(mark);
        p.setFont(monoFont(10.0 * m_scale));
        p.drawText(to + QPointF(3.0 * m_scale, -7.0 * m_scale), noteName(s.note));
    }
    m_prev = to;
    m_stroke = true;
    m_sheetInk = true;
    if (m_x > w - margin * 0.6) {
        m_stroke = false;
        turnSheet();
    }
    return true;
}

// --- Projection: the Holophonor's own light. A harmonograph figure whose
// frequency ratio is the interval from the phrase's first note, drawn in the
// theme's accent with phosphor persistence on a night ground.

bool InkPainter::stepProjection(const Synth::Snapshot &s, double dt, bool sounding, bool newNote)
{
    const qreal w = m_canvas.width(), h = m_canvas.height();
    const QPointF centre(w / 2.0, h / 2.0);
    // Phosphor decay, confined to where light was drawn.
    sinkToGround(m_lit, int(256.0 * std::pow(0.84, dt * 60.0)));

    if (sounding && (m_tonic < 0 || m_silence > 1.0))
        m_tonic = s.note;
    m_glow += ((sounding ? s.level : 0.0) - m_glow) * std::min(1.0, dt * 8.0);
    m_phase += dt * (0.35 + 0.25 * m_glow);

    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Plus);

    const int note = s.note >= 0 ? s.note : 60;
    const int fifths = (((note % 12) + 12) % 12) * 7 % 12;
    QColor light = m_accent;
    const QColor tint = pigmentFor(fifths / 12.0);
    light = QColor::fromRgbF(float(light.redF() * 0.75 + tint.redF() * 0.25),
                             float(light.greenF() * 0.75 + tint.greenF() * 0.25),
                             float(light.blueF() * 0.75 + tint.blueF() * 0.25));

    if (m_glow > 0.01) {
        const int interval = m_tonic >= 0 ? note - m_tonic : 0;
        const int semis = ((interval % 12) + 12) % 12;
        const int octaves = int(std::floor(interval / 12.0));
        qreal a = kRatios[semis][0], b = kRatios[semis][1];
        if (octaves > 0)
            a *= std::pow(2.0, std::min(octaves, 2));
        if (octaves < -1)
            b *= std::pow(2.0, std::min(-octaves - 1, 2));
        const qreal wobble = (s.pitch - std::round(s.pitch)) * 0.6; // vibrato and bends stir the figure
        const qreal radius = h * 0.32 * (0.35 + 0.65 * m_glow);

        // A damped harmonograph: the pendulums lose energy as they swing, so
        // the figure spirals inward and dense ratios read as depth, not a box.
        constexpr int points = 1400;
        constexpr qreal turns = 3.0;
        const qreal damping = std::log(4.0) / (turns * 2.0 * M_PI);
        const qreal tilt = m_phase * 0.15;
        QPolygonF figure;
        figure.reserve(points + 1);
        for (int i = 0; i <= points; ++i) {
            const qreal t = i / qreal(points) * turns * 2.0 * M_PI;
            const qreal r = radius * std::exp(-damping * t);
            const qreal x = r * std::sin(a * t + m_phase + wobble);
            const qreal y = r * 0.92 * std::sin(b * t + m_phase * 0.5);
            figure << centre + QPointF(x * std::cos(tilt) - y * std::sin(tilt), x * std::sin(tilt) + y * std::cos(tilt));
        }
        // Separate segments, not one polyline: stroking a self-crossing path
        // as a whole is several times slower, and with additive blending the
        // crossings now glow brighter, as overlapping light does.
        QVector<QLineF> segments;
        segments.reserve(figure.size());
        for (int i = 0; i + 1 < figure.size(); ++i)
            segments.append(QLineF(figure[i], figure[i + 1]));
        const struct {
            qreal width, alpha;
        } passes[] = {{5.0, 0.022}, {2.2, 0.05}, {1.0, 0.3}};
        for (const auto &pass : passes) {
            QColor c = light;
            c.setAlphaF(float(pass.alpha * m_glow));
            p.setPen(QPen(c, pass.width * m_scale, Qt::SolidLine, Qt::FlatCap));
            p.drawLines(segments);
        }
        m_lit |= figure.boundingRect().toAlignedRect().adjusted(-8, -8, 8, 8);
    }

    if (newNote && sounding)
        for (int i = 0; i < 26; ++i) {
            const qreal angle = noise() * 2.0 * M_PI;
            const QPointF dir(std::cos(angle), std::sin(angle));
            m_sparks.append({centre + dir * h * 0.05, dir * h * (0.05 + noise() * 0.22), 0.8 + noise() * 0.8});
        }
    p.setPen(Qt::NoPen);
    for (auto &spark : m_sparks) {
        spark.position += spark.velocity * dt;
        spark.velocity *= std::pow(0.4, dt);
        spark.life -= dt;
        QColor c = light;
        c.setAlphaF(float(std::clamp(spark.life, 0.0, 1.0) * 0.5));
        p.setBrush(c);
        p.drawEllipse(spark.position, 1.1 * m_scale, 1.1 * m_scale);
        m_lit |= QRect(spark.position.toPoint(), QSize(1, 1)).adjusted(-4, -4, 4, 4);
    }
    m_sparks.erase(std::remove_if(m_sparks.begin(), m_sparks.end(), [](const Spark &s) { return s.life <= 0.0; }),
                   m_sparks.end());

    // A pilot light while the projector waits for breath.
    if (m_glow < 0.05) {
        const qreal pulse = 0.5 + 0.5 * std::sin(m_time * 1.6);
        QColor c = m_accent;
        c.setAlphaF(float(0.05 + 0.05 * pulse));
        p.setPen(QPen(c, 1.0 * m_scale));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(centre, h * 0.035, h * 0.035);
        const int reach = int(h * 0.035 + 4.0 * m_scale);
        m_lit |= QRect(centre.toPoint(), QSize(1, 1)).adjusted(-reach, -reach, reach, reach);
    }
    return true;
}
