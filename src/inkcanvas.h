// The sheet in the window: drives the InkPainter once a frame from the
// synth's snapshot and hands the result to the scene graph as a texture.
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickItem>
#include <QStringList>
#include <QTimer>

#include "inkpainter.h"

class Backend;

class InkCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY modeChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY paletteChanged)
    Q_PROPERTY(QColor vermilion READ vermilion WRITE setVermilion NOTIFY paletteChanged)
    Q_PROPERTY(QStringList pigments READ pigments WRITE setPigments NOTIFY paletteChanged)

public:
    explicit InkCanvas(QQuickItem *parent = nullptr);

    QObject *source() const;
    void setSource(QObject *source);
    int mode() const { return m_painter.mode(); }
    void setMode(int mode);
    bool dark() const { return m_painter.dark(); }
    QColor accent() const { return m_accent; }
    void setAccent(const QColor &accent);
    QColor vermilion() const { return m_vermilion; }
    void setVermilion(const QColor &vermilion);
    QStringList pigments() const { return m_pigments; }
    void setPigments(const QStringList &pigments);

    Q_INVOKABLE void freshSheet();
    // Saves the painting under ~/Pictures/Holophonor and returns the path,
    // or an empty string when it couldn't be written.
    Q_INVOKABLE QString save();
    Q_INVOKABLE bool saveTo(const QString &path) const;

signals:
    void sourceChanged();
    void modeChanged();
    void paletteChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void tick();
    void relayout();

    InkPainter m_painter;
    QPointer<Backend> m_backend;
    QColor m_accent{"#d8b46a"};
    QColor m_vermilion{"#b8442e"};
    QStringList m_pigments;
    QTimer m_frame;
    QTimer m_resize;
    QElapsedTimer m_clock;
    bool m_dirty = true;
};
