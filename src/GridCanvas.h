#pragma once

#include "GridModel.h"

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QVector>
#include <QtCore/Qt>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

#include <functional>
#include <optional>

class QMouseEvent;
class QPaintEvent;
class QPainter;

struct MapRenderOptions {
    bool showGhostLayer = true;
    bool showLaserHighlights = true;
    bool showSimulationCart = true;
    bool showWarnings = true;
    bool showTriggerCounts = true;
    bool showSelection = true;
    bool showSelectionRectangle = true;
    bool showMapNorth = true;
};

class GridCanvas : public QWidget {
public:
    explicit GridCanvas(GridModel* model, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void setSelectedItem(int id);
    void setSelectedItems(const QVector<int>& ids);
    void setPlacementTool(std::optional<ItemKind> kind);
    void setCurrentLayer(int layer);
    void setSimulationCart(std::optional<LayerCell> cell);
    void setTriggerCountOverlayVisible(bool visible);
    void setHeadOnLaserIds(const QVector<int>& ids);
    void setMapNorth(Direction mapNorth);
    void refreshGeometry();
    QImage renderToImage(const MapRenderOptions& options = {}) const;
    QImage renderLayerToImage(int layer, const MapRenderOptions& options = {}) const;

    std::function<void(QPoint, std::optional<ItemKind>, Qt::KeyboardModifiers)> onCellClicked;
    std::function<bool(int, QPoint)> onItemDragged;
    std::function<void(QRect)> onRectSelectionFinished;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QPoint cellAt(const QPoint& position) const;
    QRect cellRect(const QPoint& cell, const QRect& bounds) const;
    QRect footprintRect(const Item& item, const QRect& bounds) const;
    QRect cartDirtyRect(const QPoint& cell, const QRect& bounds) const;
    QRect cellsCoveredByPixels(const QRect& bounds, const QRect& clipPixels) const;
    bool itemIntersectsClip(const Item& item, const QRect& bounds, const QRect* clipPixels) const;

    void paintMap(QPainter& painter, int layer, const MapRenderOptions& options, const QRect* clipPixels = nullptr) const;
    void drawGrid(QPainter& painter, const QRect& bounds, const QRect* clipPixels = nullptr) const;
    void drawLayer(QPainter& painter, const QRect& bounds, int layer, bool hidePlacedHandcar, const QRect* clipPixels = nullptr) const;
    void drawRail(QPainter& painter, const Item& item, const QRect& bounds) const;
    void drawItem(QPainter& painter, const Item& item, const QRect& bounds) const;
    void drawMusicMat(QPainter& painter, const Item& item, QRect rect) const;
    void drawBooBox(QPainter& painter, QRect rect) const;
    void drawBigDrum(QPainter& painter, const Item& item, const QRect& bounds) const;
    void drawLaser(QPainter& painter, const Item& item, QRect rect) const;
    void drawPinwheel(QPainter& painter, const Item& item, QRect rect) const;
    void drawHandcar(QPainter& painter, QRect rect) const;
    void drawWarnings(QPainter& painter, const QRect& bounds, int layer, const QSize& canvasSize) const;
    void drawSelection(QPainter& painter, const QRect& bounds, int layer) const;
    void drawSelectionRectangle(QPainter& painter, const QRect& bounds) const;
    void drawMapNorthIndicator(QPainter& painter, const QSize& canvasSize) const;
    void drawTriggerCountOverlay(QPainter& painter, const QRect& bounds, int layer) const;
    void drawLaserHighlights(QPainter& painter, const QRect& bounds, int layer) const;
    int triggerCountForItem(const Item& item) const;

    GridModel* m_model = nullptr;
    QVector<int> m_selectedItemIds;
    std::optional<ItemKind> m_placementTool;
    std::optional<LayerCell> m_simulationCart;
    int m_currentLayer = 0;
    int m_dragItemId = 0;
    QPoint m_dragLastCell;
    QPoint m_dragAnchorOffset;
    bool m_rectSelectionActive = false;
    QPoint m_rectSelectionStart;
    QPoint m_rectSelectionEnd;
    bool m_showTriggerCountOverlay = false;
    QVector<int> m_headOnLaserIds;
    Direction m_mapNorth = Direction::North;
};
