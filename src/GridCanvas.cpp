#include "GridCanvas.h"

#include "AppConstants.h"
#include "LaserUtils.h"

#include <QtGui/QFont>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPolygonF>

#include <algorithm>

namespace {

QRect inclusiveCellRect(const QPoint& a, const QPoint& b)
{
    return QRect(
        QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
        QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

} // namespace

GridCanvas::GridCanvas(GridModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

QSize GridCanvas::sizeHint() const
{
    const QRect bounds = m_model->visibleBounds(m_currentLayer);
    return {bounds.width() * kCellSize + 1, bounds.height() * kCellSize + 1};
}

void GridCanvas::setSelectedItem(int id)
{
    setSelectedItems(id == 0 ? QVector<int>{} : QVector<int>{id});
}

void GridCanvas::setSelectedItems(const QVector<int>& ids)
{
    m_selectedItemIds = ids;
    update();
}

void GridCanvas::setPlacementTool(std::optional<ItemKind> kind)
{
    m_placementTool = kind;
    setCursor(kind.has_value() ? Qt::CrossCursor : Qt::ArrowCursor);
}

void GridCanvas::setCurrentLayer(int layer)
{
    m_currentLayer = std::max(0, layer);
    updateGeometry();
    resize(sizeHint());
    update();
}

void GridCanvas::setSimulationCart(std::optional<LayerCell> cell)
{
    const std::optional<LayerCell> previous = m_simulationCart;
    m_simulationCart = cell;

    const bool canPartialUpdate = previous.has_value()
        && cell.has_value()
        && previous->layer == cell->layer
        && previous->layer == m_currentLayer
        && cell->layer == m_currentLayer;
    if (!canPartialUpdate) {
        update();
        return;
    }

    const QRect bounds = m_model->visibleBounds(m_currentLayer);
    update(cartDirtyRect(previous->cell, bounds));
    update(cartDirtyRect(cell->cell, bounds));
}

void GridCanvas::setTriggerCountOverlayVisible(bool visible)
{
    m_showTriggerCountOverlay = visible;
    update();
}

void GridCanvas::setHeadOnLaserIds(const QVector<int>& ids)
{
    m_headOnLaserIds = ids;
    update();
}

void GridCanvas::setMapNorth(Direction mapNorth)
{
    m_mapNorth = mapNorth;
    update();
}

void GridCanvas::refreshGeometry()
{
    updateGeometry();
    resize(sizeHint());
    update();
}

QImage GridCanvas::renderToImage(const MapRenderOptions& options) const
{
    return renderLayerToImage(m_currentLayer, options);
}

QImage GridCanvas::renderLayerToImage(int layer, const MapRenderOptions& options) const
{
    const QRect bounds = m_model->visibleBounds(layer);
    const QSize size(bounds.width() * kCellSize + 1, bounds.height() * kCellSize + 1);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#f7f7f2"));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    paintMap(painter, layer, options);
    painter.end();
    return image;
}

void GridCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPoint cell = cellAt(event->position().toPoint());
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    m_dragItemId = 0;
    m_rectSelectionActive = false;

    if (!m_placementTool.has_value()) {
        if (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (onCellClicked) {
                onCellClicked(cell, m_placementTool, modifiers);
            }
            return;
        }

        const Item* item = m_model->topItemAt(cell, m_currentLayer);
        if (!item) {
            m_rectSelectionActive = true;
            m_rectSelectionStart = cell;
            m_rectSelectionEnd = cell;
            update();
            return;
        }

        if (item) {
            m_dragItemId = item->id;
            m_dragLastCell = cell;
            m_dragAnchorOffset = cell - item->anchor;
            if (m_selectedItemIds.contains(item->id)) {
                return;
            }
        }
    }

    if (onCellClicked) {
        onCellClicked(cell, m_placementTool, modifiers);
    }
}

void GridCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_rectSelectionActive) {
        const QPoint cell = cellAt(event->position().toPoint());
        if (cell != m_rectSelectionEnd) {
            m_rectSelectionEnd = cell;
            update();
        }
        return;
    }

    if (!(event->buttons() & Qt::LeftButton) || m_dragItemId == 0 || !onItemDragged) {
        return;
    }

    const QPoint cell = cellAt(event->position().toPoint());
    if (cell == m_dragLastCell) {
        return;
    }

    const QPoint targetAnchor = cell - m_dragAnchorOffset;
    if (onItemDragged(m_dragItemId, targetAnchor)) {
        m_dragLastCell = cell;
    }
}

void GridCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (m_rectSelectionActive) {
        const QRect selectionRect = inclusiveCellRect(m_rectSelectionStart, m_rectSelectionEnd);
        m_rectSelectionActive = false;
        if (onRectSelectionFinished) {
            onRectSelectionFinished(selectionRect);
        }
        update();
    }
    m_dragItemId = 0;
}

void GridCanvas::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(event->rect());
    MapRenderOptions options;
    options.showTriggerCounts = m_showTriggerCountOverlay;
    const QRect clip = event->rect();
    paintMap(painter, m_currentLayer, options, &clip);
}

void GridCanvas::paintMap(QPainter& painter, int layer, const MapRenderOptions& options, const QRect* clipPixels) const
{
    const QRect bounds = m_model->visibleBounds(layer);
    const QSize size(bounds.width() * kCellSize + 1, bounds.height() * kCellSize + 1);
    if (clipPixels) {
        painter.fillRect(*clipPixels, QColor("#f7f7f2"));
    } else {
        painter.fillRect(QRect(QPoint(0, 0), size), QColor("#f7f7f2"));
    }

    drawGrid(painter, bounds, clipPixels);

    if (options.showGhostLayer && layer > 0) {
        painter.save();
        painter.setOpacity(0.25);
        drawLayer(painter, bounds, layer - 1, false, clipPixels);
        painter.restore();
    }

    if (options.showLaserHighlights) {
        drawLaserHighlights(painter, bounds, layer);
    }

    const bool hidePlacedHandcar = options.showSimulationCart
        && m_simulationCart.has_value()
        && m_simulationCart->layer == layer;
    drawLayer(painter, bounds, layer, hidePlacedHandcar, clipPixels);

    if (options.showSimulationCart && m_simulationCart.has_value() && m_simulationCart->layer == layer) {
        if (!clipPixels || cartDirtyRect(m_simulationCart->cell, bounds).intersects(*clipPixels)) {
            drawHandcar(painter, cellRect(m_simulationCart->cell, bounds));
        }
    }

    if (options.showWarnings) {
        drawWarnings(painter, bounds, layer, size);
    }
    if (options.showTriggerCounts) {
        drawTriggerCountOverlay(painter, bounds, layer);
    }
    if (options.showSelection) {
        drawSelection(painter, bounds, layer);
    }
    if (options.showSelectionRectangle) {
        drawSelectionRectangle(painter, bounds);
    }
    if (options.showMapNorth) {
        drawMapNorthIndicator(painter, size);
    }
}

QPoint GridCanvas::cellAt(const QPoint& position) const
{
    const QRect bounds = m_model->visibleBounds(m_currentLayer);
    return {
        bounds.left() + (position.x() / kCellSize),
        bounds.top() + (position.y() / kCellSize),
    };
}

QRect GridCanvas::cellRect(const QPoint& cell, const QRect& bounds) const
{
    return {
        (cell.x() - bounds.left()) * kCellSize,
        (cell.y() - bounds.top()) * kCellSize,
        kCellSize,
        kCellSize,
    };
}

QRect GridCanvas::footprintRect(const Item& item, const QRect& bounds) const
{
    QRect result;
    bool first = true;
    for (const QPoint& cell : m_model->occupiedCells(item)) {
        if (first) {
            result = cellRect(cell, bounds);
            first = false;
        } else {
            result = result.united(cellRect(cell, bounds));
        }
    }
    return result;
}

QRect GridCanvas::cartDirtyRect(const QPoint& cell, const QRect& bounds) const
{
    return cellRect(cell, bounds).adjusted(-4, -4, 4, 4);
}

QRect GridCanvas::cellsCoveredByPixels(const QRect& bounds, const QRect& clipPixels) const
{
    const int left = bounds.left() + clipPixels.left() / kCellSize;
    const int top = bounds.top() + clipPixels.top() / kCellSize;
    const int right = bounds.left() + (clipPixels.right() / kCellSize);
    const int bottom = bounds.top() + (clipPixels.bottom() / kCellSize);
    return QRect(QPoint(left, top), QPoint(right, bottom)).intersected(bounds);
}

bool GridCanvas::itemIntersectsClip(const Item& item, const QRect& bounds, const QRect* clipPixels) const
{
    if (!clipPixels) {
        return true;
    }
    return footprintRect(item, bounds).intersects(*clipPixels);
}

void GridCanvas::drawGrid(QPainter& painter, const QRect& bounds, const QRect* clipPixels) const
{
    painter.setPen(QPen(QColor("#d6d6d6"), 1));
    int yStart = 0;
    int yEnd = bounds.height();
    int xStart = 0;
    int xEnd = bounds.width();
    if (clipPixels) {
        const QRect cells = cellsCoveredByPixels(bounds, *clipPixels);
        xStart = std::max(0, cells.left() - bounds.left());
        xEnd = std::min(bounds.width(), cells.right() - bounds.left() + 1);
        yStart = std::max(0, cells.top() - bounds.top());
        yEnd = std::min(bounds.height(), cells.bottom() - bounds.top() + 1);
    }
    for (int y = yStart; y <= yEnd; ++y) {
        painter.drawLine(xStart * kCellSize, y * kCellSize, xEnd * kCellSize, y * kCellSize);
    }
    for (int x = xStart; x <= xEnd; ++x) {
        painter.drawLine(x * kCellSize, yStart * kCellSize, x * kCellSize, yEnd * kCellSize);
    }
}

void GridCanvas::drawLayer(QPainter& painter, const QRect& bounds, int layer, bool hidePlacedHandcar, const QRect* clipPixels) const
{
    for (const Item& item : m_model->items()) {
        if (item.layer == layer && item.kind == ItemKind::Rail && itemIntersectsClip(item, bounds, clipPixels)) {
            drawRail(painter, item, bounds);
        }
    }

    for (const Item& item : m_model->items()) {
        if (item.layer == layer && item.kind != ItemKind::Rail && item.kind != ItemKind::Handcar
            && itemIntersectsClip(item, bounds, clipPixels)) {
            drawItem(painter, item, bounds);
        }
    }

    for (const Item& item : m_model->items()) {
        if (item.layer == layer && item.kind == ItemKind::Handcar && !hidePlacedHandcar
            && itemIntersectsClip(item, bounds, clipPixels)) {
            drawHandcar(painter, cellRect(item.anchor, bounds));
        }
    }
}

void GridCanvas::drawRail(QPainter& painter, const Item& item, const QRect& bounds) const
{
    const QRect rect = cellRect(item.anchor, bounds).adjusted(3, 3, -3, -3);
    const QPointF center = rect.center();
    const QVector<LayerCell> neighbors = m_model->railNeighbors({item.anchor, item.layer});

    painter.save();
    painter.setPen(QPen(QColor("#666666"), 8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (neighbors.isEmpty()) {
        painter.drawEllipse(center, 5, 5);
    } else {
        bool connectsUp = false;
        bool connectsDown = false;
        for (const LayerCell& neighbor : neighbors) {
            connectsUp = connectsUp || neighbor.layer > item.layer;
            connectsDown = connectsDown || neighbor.layer < item.layer;
            const QPoint delta = neighbor.cell - item.anchor;
            QPointF end = center;
            if (delta.x() < 0) {
                end.setX(rect.left());
            } else if (delta.x() > 0) {
                end.setX(rect.right());
            } else if (delta.y() < 0) {
                end.setY(rect.top());
            } else if (delta.y() > 0) {
                end.setY(rect.bottom());
            }
            painter.drawLine(center, end);
        }
        if (connectsUp || connectsDown) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#3d5a80"));
            QPolygonF marker;
            if (connectsUp) {
                marker << QPointF(rect.center().x(), rect.top() + 5) << QPointF(rect.left() + 7, rect.center().y()) << QPointF(rect.right() - 7, rect.center().y());
            } else {
                marker << QPointF(rect.center().x(), rect.bottom() - 5) << QPointF(rect.left() + 7, rect.center().y()) << QPointF(rect.right() - 7, rect.center().y());
            }
            painter.drawPolygon(marker);
        }
    }
    painter.setPen(QPen(QColor("#bdbdbd"), 2, Qt::SolidLine, Qt::RoundCap));
    if (neighbors.size() >= 2) {
        painter.drawEllipse(center, 3, 3);
    }
    painter.restore();
}

void GridCanvas::drawItem(QPainter& painter, const Item& item, const QRect& bounds) const
{
    if (isMusicMat(item.kind)) {
        drawMusicMat(painter, item, cellRect(item.anchor, bounds));
        return;
    }
    switch (item.kind) {
    case ItemKind::BooBox:
        drawBooBox(painter, cellRect(item.anchor, bounds));
        break;
    case ItemKind::BigDrum:
        drawBigDrum(painter, item, bounds);
        break;
    case ItemKind::Laser:
        drawLaser(painter, item, cellRect(item.anchor, bounds));
        break;
    case ItemKind::Pinwheel:
        drawPinwheel(painter, item, cellRect(item.anchor, bounds));
        break;
    default:
        break;
    }
}

void GridCanvas::drawMusicMat(QPainter& painter, const Item& item, QRect rect) const
{
    rect.adjust(4, 4, -4, -4);
    painter.save();
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.setBrush(musicMatColor(item.kind));
    painter.drawRoundedRect(rect, 4, 4);

    painter.translate(rect.center());
    painter.rotate(item.rotationQuarters * 90.0);
    QPainterPath note;
    note.addEllipse(QRectF(-8, 5, 10, 7));
    note.addRect(QRectF(1, -10, 3, 18));
    note.moveTo(3, -10);
    note.cubicTo(10, -11, 12, -6, 8, -3);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#222222"));
    painter.drawPath(note);
    painter.restore();
}

void GridCanvas::drawBooBox(QPainter& painter, QRect rect) const
{
    rect.adjust(5, 5, -5, -5);
    painter.save();
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.setBrush(QColor("#f8f1d8"));
    painter.drawRect(rect);
    painter.setBrush(QColor("#be5a38"));
    painter.drawRect(rect.adjusted(3, rect.height() / 2, -3, -3));
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.drawLine(rect.center().x(), rect.top() + 4, rect.center().x(), rect.bottom() - 4);
    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.drawText(rect, Qt::AlignCenter, "BOO");
    painter.restore();
}

void GridCanvas::drawBigDrum(QPainter& painter, const Item& item, const QRect& bounds) const
{
    QRect rect = footprintRect(item, bounds).adjusted(4, 4, -4, -4);
    painter.save();
    painter.setPen(QPen(QColor("#333333"), 2, Qt::DashLine));
    painter.setBrush(QColor("#d9a441"));
    painter.drawRoundedRect(rect, 8, 8);

    painter.setPen(QPen(QColor("#333333"), 1, Qt::DotLine));
    if (item.rotationQuarters % 2 == 0) {
        painter.drawLine(rect.left(), rect.top() + rect.height() / 2, rect.right(), rect.top() + rect.height() / 2);
    } else {
        painter.drawLine(rect.left() + rect.width() / 2, rect.top(), rect.left() + rect.width() / 2, rect.bottom());
    }

    const QRect drum = QRect(rect.center().x() - 18, rect.center().y() - 13, 36, 26);
    painter.setPen(QPen(QColor("#4a2d1f"), 2));
    painter.setBrush(QColor("#f6d186"));
    painter.drawEllipse(drum);
    painter.setFont(QFont("Arial", 7, QFont::Bold));
    painter.drawText(drum, Qt::AlignCenter, "DRUM");
    painter.restore();
}

void GridCanvas::drawLaser(QPainter& painter, const Item& item, QRect rect) const
{
    rect.adjust(5, 5, -5, -5);
    painter.save();
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.setBrush(QColor("#b8a1ff"));
    painter.drawRoundedRect(rect, 5, 5);

    painter.translate(rect.center());
    painter.rotate(item.rotationQuarters * 90.0);
    QPolygonF arrow;
    arrow << QPointF(0, -12) << QPointF(8, 4) << QPointF(2, 3) << QPointF(2, 12) << QPointF(-2, 12) << QPointF(-2, 3) << QPointF(-8, 4);
    painter.setBrush(QColor("#382c74"));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(arrow);
    painter.restore();
}

void GridCanvas::drawPinwheel(QPainter& painter, const Item&, QRect rect) const
{
    rect.adjust(5, 5, -5, -5);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.setBrush(QColor("#f4e8ff"));
    painter.drawEllipse(rect);

    painter.translate(rect.center());
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 4; ++i) {
        painter.save();
        painter.rotate(i * 90.0);
        QPainterPath blade;
        blade.moveTo(0, 0);
        blade.cubicTo(6, -14, 18, -13, 17, -2);
        blade.lineTo(3, 3);
        blade.closeSubpath();
        painter.setBrush(i % 2 == 0 ? QColor("#ffcf56") : QColor("#80d8ff"));
        painter.drawPath(blade);
        painter.restore();
    }
    painter.setBrush(QColor("#ffffff"));
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.drawEllipse(QPointF(0, 0), 5, 5);
    painter.restore();
}

void GridCanvas::drawHandcar(QPainter& painter, QRect rect) const
{
    rect.adjust(5, 8, -5, -7);
    painter.save();
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.setBrush(QColor("#c65d3a"));
    painter.drawRoundedRect(rect.adjusted(2, 3, -2, -5), 4, 4);
    painter.setBrush(QColor("#222222"));
    painter.drawEllipse(QRect(rect.left() + 4, rect.bottom() - 7, 7, 7));
    painter.drawEllipse(QRect(rect.right() - 11, rect.bottom() - 7, 7, 7));
    painter.setPen(QPen(QColor("#333333"), 2));
    painter.drawLine(rect.center().x(), rect.top(), rect.center().x(), rect.top() + 8);
    painter.restore();
}

void GridCanvas::drawWarnings(QPainter& painter, const QRect& bounds, int layer, const QSize& canvasSize) const
{
    for (const LayerCell& cell : m_model->tJunctionCells()) {
        if (cell.layer != layer) {
            continue;
        }
        const QRect rect = cellRect(cell.cell, bounds).adjusted(7, 7, -7, -7);
        QPolygonF triangle;
        triangle << QPointF(rect.center().x(), rect.top()) << QPointF(rect.right(), rect.bottom()) << QPointF(rect.left(), rect.bottom());
        painter.save();
        painter.setPen(QPen(QColor("#513c00"), 2));
        painter.setBrush(QColor("#ffd166"));
        painter.drawPolygon(triangle);
        painter.setPen(QColor("#513c00"));
        painter.setFont(QFont("Arial", 11, QFont::Bold));
        painter.drawText(rect, Qt::AlignCenter, "!");
        painter.restore();
    }

    if (m_headOnLaserIds.isEmpty()) {
        return;
    }

    // Highlight head-on lasers on the grid
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 120, 0, 60));
    for (const Item& item : m_model->items()) {
        if (item.kind != ItemKind::Laser || item.layer != layer) {
            continue;
        }
        if (!m_headOnLaserIds.contains(item.id)) {
            continue;
        }
        painter.drawRect(cellRect(item.anchor, bounds).adjusted(1, 1, -1, -1));
        for (const LayerCell& beamCell : laserBeamCells(item)) {
            if (beamCell.layer == layer) {
                painter.drawRect(cellRect(beamCell.cell, bounds).adjusted(1, 1, -1, -1));
            }
        }
    }
    painter.restore();

    // Warning panel in the bottom-right corner of the visible area
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    const int panelW = 290;
    const int panelH = 52;
    const int margin = 10;
    const QRect panelRect(
        canvasSize.width() - panelW - margin,
        canvasSize.height() - panelH - margin,
        panelW,
        panelH);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(60, 30, 0, 200));
    painter.drawRoundedRect(panelRect, 8, 8);

    painter.setPen(QPen(QColor("#ff8c00"), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(panelRect.adjusted(1, 1, -1, -1), 7, 7);

    // Warning icon (triangle)
    const int iconX = panelRect.left() + 12;
    const int iconY = panelRect.top() + (panelRect.height() - 22) / 2;
    QPolygonF warnTri;
    warnTri << QPointF(iconX + 11, iconY)
            << QPointF(iconX + 22, iconY + 19)
            << QPointF(iconX, iconY + 19);
    painter.setPen(QPen(QColor("#513c00"), 1.5));
    painter.setBrush(QColor("#ffd166"));
    painter.drawPolygon(warnTri);
    painter.setPen(QColor("#513c00"));
    painter.setFont(QFont("Arial", 8, QFont::Bold));
    painter.drawText(QRect(iconX, iconY + 4, 22, 15), Qt::AlignCenter, "!");

    // Message text
    painter.setPen(QColor("#ffe0a0"));
    painter.setFont(QFont("Arial", 8));
    const QRect textRect(panelRect.left() + 40, panelRect.top(), panelRect.width() - 48, panelRect.height());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::TextWordWrap,
        "Feixes acionados de frente pelo carrinho podem disparar antes dos demais no jogo.");
    painter.restore();
}

void GridCanvas::drawSelection(QPainter& painter, const QRect& bounds, int layer) const
{
    painter.save();
    painter.setPen(QPen(QColor("#246bfe"), 3));
    painter.setBrush(Qt::NoBrush);
    for (int id : m_selectedItemIds) {
        const Item* selected = m_model->findItem(id);
        if (!selected || selected->layer != layer) {
            continue;
        }
        painter.drawRect(footprintRect(*selected, bounds).adjusted(2, 2, -2, -2));
    }
    painter.restore();
}

void GridCanvas::drawSelectionRectangle(QPainter& painter, const QRect& bounds) const
{
    if (!m_rectSelectionActive) {
        return;
    }

    const QRect cellSelection = inclusiveCellRect(m_rectSelectionStart, m_rectSelectionEnd);
    QRect drawRect;
    bool first = true;
    for (int row = cellSelection.top(); row <= cellSelection.bottom(); ++row) {
        for (int col = cellSelection.left(); col <= cellSelection.right(); ++col) {
            const QRect cell = cellRect(QPoint(col, row), bounds);
            drawRect = first ? cell : drawRect.united(cell);
            first = false;
        }
    }

    painter.save();
    painter.setPen(QPen(QColor("#246bfe"), 2, Qt::DashLine));
    painter.setBrush(QColor(36, 107, 254, 35));
    painter.drawRect(drawRect.adjusted(1, 1, -1, -1));
    painter.restore();
}

void GridCanvas::drawTriggerCountOverlay(QPainter& painter, const QRect& bounds, int layer) const
{
    painter.save();
    for (const Item& item : m_model->items()) {
        if (item.layer != layer || !isSoundItem(item.kind)) {
            continue;
        }

        const QRect itemRect = footprintRect(item, bounds).adjusted(3, 3, -3, -3);
        const int count = triggerCountForItem(item);
        const int badgeSize = 18;
        QRect badge(
            itemRect.right() - badgeSize + 1,
            itemRect.top(),
            badgeSize,
            badgeSize);

        painter.setPen(QPen(QColor("#111111"), 1));
        painter.setBrush(count > 0 ? QColor("#ffffff") : QColor("#e6e6e6"));
        painter.drawEllipse(badge);
        painter.setPen(count > 0 ? QColor("#111111") : QColor("#777777"));
        painter.setFont(QFont("Arial", 8, QFont::Bold));
        painter.drawText(badge, Qt::AlignCenter, QString::number(count));
    }
    painter.restore();
}

void GridCanvas::drawLaserHighlights(QPainter& painter, const QRect& bounds, int layer) const
{
    if (m_selectedItemIds.size() != 1) {
        return;
    }
    const Item* selected = m_model->findItem(m_selectedItemIds.first());
    if (!selected || selected->layer != layer) {
        return;
    }
    painter.save();
    painter.setPen(Qt::NoPen);
    if (selected->kind == ItemKind::Laser) {
        painter.setBrush(QColor(80, 130, 255, 55));
        for (const LayerCell& cell : laserSoundTargetCells(*selected)) {
            if (cell.layer == layer || cell.layer == layer - 1) {
                painter.drawRect(cellRect(cell.cell, bounds).adjusted(2, 2, -2, -2));
            }
        }
    } else if (selected->kind == ItemKind::Pinwheel) {
        const LayerCell pinwheelCell{selected->anchor, selected->layer};
        painter.setBrush(QColor(255, 180, 60, 65));
        for (const Item* laser : m_model->laserSensors()) {
            if (!laserBeamCells(*laser).contains(pinwheelCell)) {
                continue;
            }
            painter.drawRect(cellRect(laser->anchor, bounds).adjusted(2, 2, -2, -2));
            for (const LayerCell& beamCell : laserBeamCells(*laser)) {
                painter.drawRect(cellRect(beamCell.cell, bounds).adjusted(2, 2, -2, -2));
            }
        }
    }
    painter.restore();
}

int GridCanvas::triggerCountForItem(const Item& item) const
{
    int count = 0;
    const QVector<LayerCell> occupiedCells = m_model->occupiedLayerCells(item);
    for (const Item* laser : m_model->laserSensors()) {
        const QVector<LayerCell> targets = laserSoundTargetCells(*laser);
        for (const LayerCell& cell : occupiedCells) {
            if (targets.contains(cell)) {
                ++count;
                break;
            }
        }
    }
    return count;
}

void GridCanvas::drawMapNorthIndicator(QPainter& painter, const QSize& canvasSize) const
{
    const QPoint center(canvasSize.width() - 28, 28);
    painter.save();
    painter.setPen(QPen(QColor("#555555"), 2));
    painter.setBrush(QColor(255, 255, 255, 210));
    painter.drawEllipse(center, 20, 20);

    painter.translate(center);
    painter.rotate(directionToRotation(m_mapNorth) * 90.0);
    painter.setPen(QPen(QColor("#c62828"), 2));
    painter.setBrush(QColor("#e53935"));
    QPolygonF arrow;
    arrow << QPointF(0, -12) << QPointF(-5, 2) << QPointF(0, -1) << QPointF(5, 2);
    painter.drawPolygon(arrow);

    painter.rotate(-directionToRotation(m_mapNorth) * 90.0);
    painter.setPen(QColor("#333333"));
    painter.setFont(QFont("Arial", 8, QFont::Bold));
    painter.drawText(QRect(-20, 6, 40, 14), Qt::AlignCenter, "N");
    painter.restore();
}
