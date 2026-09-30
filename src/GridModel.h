#pragma once

#include "ItemTypes.h"

#include <QtCore/QHash>
#include <QtCore/QRect>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <optional>

class GridModel {
public:
    GridModel() = default;
    GridModel(const GridModel& other);
    GridModel& operator=(const GridModel& other);
    GridModel(GridModel&& other) noexcept;
    GridModel& operator=(GridModel&& other) noexcept;

    const QVector<Item>& items() const;

    void clear();
    bool isDirty() const;
    void setDirty(bool dirty);

    Item* findItem(int id);
    const Item* findItem(int id) const;

    QVector<QPoint> occupiedCells(const Item& item) const;
    QVector<LayerCell> occupiedLayerCells(const Item& item) const;
    QVector<const Item*> itemsAt(const QPoint& cell, int layer) const;
    QVector<const Item*> itemsOccupying(const QPoint& cell, int layer) const;
    const Item* topItemAt(const QPoint& cell, int layer) const;
    const Item* firstSoundItemAt(const QPoint& cell, int layer) const;

    bool hasRailAt(const QPoint& cell, int layer) const;
    bool hasHandcarAt(const QPoint& cell, int layer) const;
    bool hasHandcarAnywhere(int ignoredId = -1) const;
    bool canPlace(ItemKind kind, const QPoint& anchor, int layer, int rotationQuarters, int ignoredId, QString* reason = nullptr) const;
    bool addItem(ItemKind kind, const QPoint& anchor, int layer, int rotationQuarters, int* newId = nullptr, QString* reason = nullptr);
    bool removeItem(int id);
    bool removeItems(const QVector<int>& ids);
    bool rotateItem(int id, QString* reason = nullptr);
    bool moveItem(int id, const QPoint& newAnchor, QString* reason = nullptr);
    bool moveItems(const QVector<int>& ids, const QPoint& delta, QString* reason = nullptr);
    void remapMusicMatRotationsForMapNorth(Direction oldMapNorth, Direction newMapNorth);
    QVector<int> itemIdsInRect(const QRect& rect, int layer) const;

    QRect contentBounds(int layer) const;
    QRect visibleBounds(int layer) const;
    int maxLayer() const;
    void shiftLayersUp(int fromLayer);

    QVector<LayerCell> railNeighbors(const LayerCell& cell) const;
    int railDegree(const LayerCell& cell) const;
    QVector<LayerCell> tJunctionCells() const;
    bool hasTJunction() const;
    std::optional<LayerCell> firstHandcarCell() const;
    QVector<const Item*> laserSensors() const;

    QString playValidationMessage(Direction initialDirection) const;
    QVector<int> headOnLaserIds(Direction initialDirection) const;

private:
    bool buildModelFromItems(const QVector<Item>& items, QString* reason = nullptr) const;
    void invalidateIndexes() const;
    void ensureIndexes() const;
    void indexItemAt(int index) const;
    const Item* itemById(int id) const;
    QVector<const Item*> itemsFromIds(const QVector<int>& ids) const;

    QVector<Item> m_items;
    int m_nextId = 1;
    bool m_dirty = false;

    mutable bool m_indexDirty = true;
    mutable QHash<int, int> m_itemIndexById;
    mutable QHash<LayerCell, QVector<int>> m_occupancyByCell;
    mutable QSet<LayerCell> m_railCells;
    mutable QVector<int> m_laserIds;
};
