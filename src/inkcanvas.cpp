#include "inkcanvas.h"

#include <QDateTime>
#include <QDir>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

#include "backend.h"

namespace {

// Above this many pixels the sheet is painted smaller and scaled up: ink is
// soft-edged, and uploading the whole sheet every frame has to stay cheap.
constexpr qreal kMaxPixels = 3.2e6;

// Owns its texture so the previous frame's is freed when a new one arrives.
class SheetNode : public QSGSimpleTextureNode {
public:
    ~SheetNode() override { delete texture(); }
    void replace(QSGTexture *next)
    {
        QSGTexture *previous = texture();
        setTexture(next);
        delete previous;
    }
};

} // namespace

InkCanvas::InkCanvas(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setActiveFocusOnTab(true);

    m_frame.setInterval(16);
    m_frame.setTimerType(Qt::PreciseTimer);
    connect(&m_frame, &QTimer::timeout, this, &InkCanvas::tick);
    m_frame.start();
    m_clock.start();

    // Regenerating paper is the one expensive step; wait for a resize to settle.
    m_resize.setSingleShot(true);
    m_resize.setInterval(120);
    connect(&m_resize, &QTimer::timeout, this, &InkCanvas::relayout);
}

QObject *InkCanvas::source() const { return m_backend.data(); }

void InkCanvas::setSource(QObject *source)
{
    auto *typed = qobject_cast<Backend *>(source);
    if (typed == m_backend)
        return;
    m_backend = typed;
    emit sourceChanged();
}

void InkCanvas::setMode(int mode)
{
    if (mode == m_painter.mode())
        return;
    m_painter.setMode(mode);
    emit modeChanged();
}

void InkCanvas::setAccent(const QColor &accent)
{
    if (accent == m_accent)
        return;
    m_accent = accent;
    m_painter.setAccent(accent);
    emit paletteChanged();
}

void InkCanvas::setVermilion(const QColor &vermilion)
{
    if (vermilion == m_vermilion)
        return;
    m_vermilion = vermilion;
    m_painter.setVermilion(vermilion);
    emit paletteChanged();
}

void InkCanvas::setPigments(const QStringList &pigments)
{
    if (pigments == m_pigments)
        return;
    m_pigments = pigments;
    QVector<QColor> colours;
    for (const QString &name : pigments)
        if (QColor c(name); c.isValid())
            colours.append(c);
    m_painter.setPigments(colours);
    emit paletteChanged();
}

void InkCanvas::freshSheet()
{
    m_painter.freshSheet();
    m_dirty = true;
    update();
}

bool InkCanvas::saveTo(const QString &path) const
{
    // QSaveFile writes beside the target and renames: never a half-written PNG.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (!m_painter.image().save(&file, "PNG"))
        return false;
    return file.commit();
}

QString InkCanvas::save()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Holophonor";
    if (!QDir().mkpath(folder))
        return {};
    const QString path =
        folder + "/holophonor-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png";
    return saveTo(path) ? path : QString();
}

void InkCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    // The first size arrives at startup: paint paper at once, not after a beat.
    if (m_painter.size().isEmpty())
        relayout();
    else
        m_resize.start();
}

void InkCanvas::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange)
        m_resize.start();
}

void InkCanvas::relayout()
{
    if (width() <= 0 || height() <= 0)
        return;
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal pixels = width() * height() * dpr * dpr;
    const qreal scale = dpr * std::min(1.0, std::sqrt(kMaxPixels / pixels));
    m_painter.resize(QSize(int(std::round(width() * scale)), int(std::round(height() * scale))), scale);
    m_dirty = true;
    update();
}

void InkCanvas::tick()
{
    const double dt = m_clock.restart() / 1000.0;
    if (!m_backend || !isVisible())
        return;
    if (m_painter.step(m_backend->snapshot(), dt)) {
        m_dirty = true;
        update();
    }
}

QSGNode *InkCanvas::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (m_painter.image().isNull()) {
        delete old;
        return nullptr;
    }
    auto *node = static_cast<SheetNode *>(old);
    if (!node) {
        node = new SheetNode;
        node->setFiltering(QSGTexture::Linear);
        m_dirty = true;
    }
    if (m_dirty) {
        node->replace(window()->createTextureFromImage(m_painter.image()));
        m_dirty = false;
    }
    node->setRect(boundingRect());
    return node;
}
