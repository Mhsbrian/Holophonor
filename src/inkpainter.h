// Paints the music. Each frame it reads one synth snapshot and adds to a
// persistent image, the way a brush adds to paper: nothing is redrawn from
// scratch, so a performance accumulates into a picture worth saving.
#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRandomGenerator>
#include <QVector>

#include "synth.h"

class QPainter;

class InkPainter {
public:
    enum Mode { Sumi, Bloom, Graphite, Projection, ModeCount };

    InkPainter();

    // `scale` is image pixels per logical pixel, so strokes keep their size.
    void resize(QSize pixels, qreal scale);
    QSize size() const { return m_canvas.size(); }

    void setMode(int mode);
    int mode() const { return m_mode; }
    bool dark() const { return m_mode == Projection; }

    void setAccent(const QColor &accent) { m_accent = accent; }
    void setVermilion(const QColor &vermilion) { m_vermilion = vermilion; }
    void setPigments(const QVector<QColor> &pigments);

    void freshSheet();

    // Advances by `dt` seconds. Returns true when the image changed.
    bool step(const Synth::Snapshot &snapshot, double dt);
    const QImage &image() const { return m_canvas; }

    static QString noteName(int note);
    // Where a pitch sits on the sheet, 0 at the bottom margin to 1 at the top.
    static qreal pitchPosition(qreal pitch);

private:
    struct Spark {
        QPointF position, velocity;
        double life = 0;
    };

    const QImage &ground() const { return m_mode == Projection ? m_night : m_paper; }
    void blendGround(qreal opacity);
    void sinkToGround(QRect area, int keep);
    qreal brushRadius(qreal level) const;
    void resetPen();
    void turnSheet();
    qreal yFor(qreal pitch) const;
    QColor pigmentFor(qreal turn) const;
    qreal noise() { return m_random.generateDouble(); }
    qreal gaussian();
    QPolygonF deform(const QPolygonF &shape, qreal variance);

    bool stepSumi(const Synth::Snapshot &s, double dt, bool sounding);
    bool stepBloom(const Synth::Snapshot &s, double dt, bool sounding, bool newNote);
    bool stepGraphite(const Synth::Snapshot &s, double dt, bool sounding, bool newNote);
    bool stepProjection(const Synth::Snapshot &s, double dt, bool sounding, bool newNote);

    void brush(const QPointF &from, const QPointF &to, qreal level);
    void stamp(QPainter &p, const QPointF &at, qreal radius, qreal opacity, qreal angle, bool dry);
    void placeSeal();
    void drawGrid();

    QImage m_paper, m_night, m_canvas;
    QImage m_tip, m_dryTip;
    qreal m_scale = 1.0;
    int m_mode = Sumi;
    QColor m_accent{"#d8b46a"};
    QColor m_vermilion{"#b8442e"};
    QVector<QColor> m_pigments;
    QRandomGenerator m_random{20261011};

    double m_time = 0.0;
    double m_silence = 10.0;    // seconds since anything sounded
    double m_transition = 0.0;  // seconds left of a ground cross-fade
    double m_absorb = 0.0;      // seconds left of ink sinking into the sheet
    double m_absorbRate = 0.0;
    uint32_t m_serial = 0;

    // The pen, shared by the modes that write across the sheet.
    qreal m_x = 0.0;
    qreal m_penY = 0.0;
    QPointF m_prev;
    bool m_stroke = false;
    bool m_sheetInk = false;
    bool m_sealed = false;
    qreal m_inkLoad = 1.0;
    qreal m_carry = 0.0;
    qreal m_lastHatch = 0.0;

    // Bloom.
    QPolygonF m_bloomShape;
    QPointF m_bloomCentre;
    QColor m_bloomColour;
    qreal m_bloomRadius = 0.0, m_bloomMax = 0.0;
    int m_bloomFrames = 0;
    bool m_blooming = false;
    bool m_hasBloom = false;
    int m_bloomNote = -1;
    int m_blooms = 0;

    // Graphite.
    bool m_gridDrawn = false;

    // Projection.
    double m_phase = 0.0;
    double m_glow = 0.0;
    int m_tonic = -1;
    QVector<Spark> m_sparks;
    QRect m_lit; // where light has been drawn and must fade
};
