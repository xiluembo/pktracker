#include "GridModel.h"

#include "AppConstants.h"
#include "LaserUtils.h"

#include <algorithm>
#include <limits>

namespace {

int normalizedRotation(int rotationQuarters)
{
    return ((rotationQuarters % 4) + 4) % 4;
}

bool occupiesLayerAbove(ItemKind kind)
{
    return kind == ItemKind::BooBox || kind == ItemKind::BigDrum;
}

int itemDrawRank(ItemKind kind)
{
    if (kind == ItemKind::Handcar) {
        return 4;
    }
    if (kind == ItemKind::Laser) {
        return 3;
    }
    if (kind == ItemKind::Pinwheel) {
        return 3;
    }
    if (isSoundItem(kind)) {
        return 2;
    }
    if (kind == ItemKind::Rail) {
        return 0;
    }
    return 1;
}

} // namespace

GridModel::GridModel(const GridModel& other)
    : m_items(other.m_items)
    , m_nextId(other.m_nextId)
    , m_dirty(other.m_dirty)
    , m_indexDirty(true)
{
}

GridModel& GridModel::operator=(const GridModel& other)
{
    if (this != &other) {
        m_items = other.m_items;
        m_nextId = other.m_nextId;
        m_dirty = other.m_dirty;
        invalidateIndexes();
    }
    return *this;
}

GridModel::GridModel(GridModel&& other) noexcept
    : m_items(std::move(other.m_items))
    , m_nextId(other.m_nextId)
    , m_dirty(other.m_dirty)
    , m_indexDirty(true)
{
    other.m_nextId = 1;
    other.m_dirty = false;
    other.invalidateIndexes();
}

GridModel& GridModel::operator=(GridModel&& other) noexcept
{
    if (this != &other) {
        m_items = std::move(other.m_items);
        m_nextId = other.m_nextId;
        m_dirty = other.m_dirty;
        invalidateIndexes();
        other.m_nextId = 1;
        other.m_dirty = false;
        other.invalidateIndexes();
    }
    return *this;
}

const QVector<Item>& GridModel::items() const
{
    return m_items;
}

void GridModel::invalidateIndexes() const
{
    m_indexDirty = true;
    m_itemIndexById.clear();
    m_occupancyByCell.clear();
    m_railCells.clear();
    m_laserIds.clear();
}

void GridModel::indexItemAt(int index) const
{
    const Item& item = m_items.at(index);
    m_itemIndexById.insert(item.id, index);
    for (const LayerCell& cell : occupiedLayerCells(item)) {
        m_occupancyByCell[cell].push_back(item.id);
    }
    if (item.kind == ItemKind::Rail) {
        m_railCells.insert({item.anchor, item.layer});
    }
    if (item.kind == ItemKind::Laser) {
        m_laserIds.push_back(item.id);
    }
}

const Item* GridModel::itemById(int id) const
{
    const auto it = m_itemIndexById.constFind(id);
    if (it == m_itemIndexById.cend()) {
        return nullptr;
    }
    const int index = it.value();
    if (index < 0 || index >= m_items.size()) {
        return nullptr;
    }
    return &m_items[index];
}

void GridModel::ensureIndexes() const
{
    if (!m_indexDirty) {
        return;
    }

    m_itemIndexById.clear();
    m_occupancyByCell.clear();
    m_railCells.clear();
    m_laserIds.clear();

    for (int i = 0; i < m_items.size(); ++i) {
        indexItemAt(i);
    }

    m_indexDirty = false;
}

QVector<const Item*> GridModel::itemsFromIds(const QVector<int>& ids) const
{
    QVector<const Item*> found;
    found.reserve(ids.size());
    for (int id : ids) {
        if (const Item* item = itemById(id)) {
            found.push_back(item);
        }
    }
    std::sort(found.begin(), found.end(), [](const Item* a, const Item* b) {
        return itemDrawRank(a->kind) < itemDrawRank(b->kind);
    });
    return found;
}

void GridModel::clear()
{
    m_items.clear();
    m_nextId = 1;
    m_dirty = false;
    invalidateIndexes();
}

bool GridModel::isDirty() const
{
    return m_dirty;
}

void GridModel::setDirty(bool dirty)
{
    m_dirty = dirty;
}

Item* GridModel::findItem(int id)
{
    ensureIndexes();
    return const_cast<Item*>(itemById(id));
}

const Item* GridModel::findItem(int id) const
{
    ensureIndexes();
    return itemById(id);
}

QVector<QPoint> GridModel::occupiedCells(const Item& item) const
{
    if (item.kind == ItemKind::BigDrum) {
        if (item.rotationQuarters % 2 == 0) {
            return {item.anchor, item.anchor + QPoint(0, 1)};
        }
        return {item.anchor, item.anchor + QPoint(1, 0)};
    }
    return {item.anchor};
}

QVector<LayerCell> GridModel::occupiedLayerCells(const Item& item) const
{
    QVector<LayerCell> cells;
    for (const QPoint& cell : occupiedCells(item)) {
        cells.push_back({cell, item.layer});
        if (occupiesLayerAbove(item.kind)) {
            cells.push_back({cell, item.layer + 1});
        }
    }
    return uniqueLayerCells(cells);
}

QVector<const Item*> GridModel::itemsAt(const QPoint& cell, int layer) const
{
    ensureIndexes();
    QVector<const Item*> found;
    for (int id : m_occupancyByCell.value({cell, layer})) {
        const Item* item = itemById(id);
        if (item && item->layer == layer) {
            found.push_back(item);
        }
    }
    std::sort(found.begin(), found.end(), [](const Item* a, const Item* b) {
        return itemDrawRank(a->kind) < itemDrawRank(b->kind);
    });
    return found;
}

QVector<const Item*> GridModel::itemsOccupying(const QPoint& cell, int layer) const
{
    ensureIndexes();
    return itemsFromIds(m_occupancyByCell.value({cell, layer}));
}

const Item* GridModel::topItemAt(const QPoint& cell, int layer) const
{
    const QVector<const Item*> found = itemsAt(cell, layer);
    if (found.isEmpty()) {
        return nullptr;
    }
    return found.back();
}

const Item* GridModel::firstSoundItemAt(const QPoint& cell, int layer) const
{
    const QVector<const Item*> found = itemsAt(cell, layer);
    for (auto it = found.crbegin(); it != found.crend(); ++it) {
        if (isSoundItem((*it)->kind)) {
            return *it;
        }
    }
    return nullptr;
}

bool GridModel::hasRailAt(const QPoint& cell, int layer) const
{
    ensureIndexes();
    return m_railCells.contains({cell, layer});
}

bool GridModel::hasHandcarAt(const QPoint& cell, int layer) const
{
    for (const Item& item : m_items) {
        if (item.kind == ItemKind::Handcar && item.anchor == cell && item.layer == layer) {
            return true;
        }
    }
    return false;
}

bool GridModel::hasHandcarAnywhere(int ignoredId) const
{
    for (const Item& item : m_items) {
        if (item.kind == ItemKind::Handcar && item.id != ignoredId) {
            return true;
        }
    }
    return false;
}

bool GridModel::canPlace(ItemKind kind, const QPoint& anchor, int layer, int rotationQuarters, int ignoredId, QString* reason) const
{
    const Item candidate{-1, kind, anchor, normalizedRotation(rotationQuarters), layer};
    const QVector<LayerCell> candidateCells = occupiedLayerCells(candidate);

    if (kind == ItemKind::Handcar) {
        if (!hasRailAt(anchor, layer)) {
            if (reason) {
                *reason = "A handcar must be placed on a railway track.";
            }
            return false;
        }
        if (hasHandcarAnywhere(ignoredId)) {
            if (reason) {
                *reason = "Only one handcar is allowed per map.";
            }
            return false;
        }
    }

    ensureIndexes();
    for (const LayerCell& cell : candidateCells) {
        for (int existingId : m_occupancyByCell.value(cell)) {
            if (existingId == ignoredId) {
                continue;
            }
            const Item* existing = itemById(existingId);
            if (!existing) {
                continue;
            }
            if (kind == ItemKind::Handcar && existing->kind == ItemKind::Rail && existing->layer == layer) {
                continue;
            }
            if (kind == ItemKind::Rail && existing->kind == ItemKind::Handcar && existing->layer == layer) {
                continue;
            }
            if (reason) {
                *reason = "This cell is already occupied.";
            }
            return false;
        }
    }

    return true;
}

bool GridModel::addItem(ItemKind kind, const QPoint& anchor, int layer, int rotationQuarters, int* newId, QString* reason)
{
    if (!canPlace(kind, anchor, layer, rotationQuarters, -1, reason)) {
        return false;
    }
    Item item{m_nextId++, kind, anchor, normalizedRotation(rotationQuarters), layer};
    m_items.push_back(item);
    if (newId) {
        *newId = item.id;
    }
    m_dirty = true;
    if (m_indexDirty) {
        invalidateIndexes();
    } else {
        indexItemAt(m_items.size() - 1);
    }
    return true;
}

bool GridModel::removeItem(int id)
{
    const Item* item = findItem(id);
    if (!item) {
        return false;
    }
    const bool removingRail = item->kind == ItemKind::Rail;
    const QPoint railCell = item->anchor;
    const int railLayer = item->layer;
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const Item& current) {
                      return current.id == id || (removingRail && current.kind == ItemKind::Handcar && current.anchor == railCell && current.layer == railLayer);
                  }),
        m_items.end());
    m_dirty = true;
    invalidateIndexes();
    return true;
}

bool GridModel::removeItems(const QVector<int>& ids)
{
    bool removed = false;
    for (int id : ids) {
        if (findItem(id)) {
            removeItem(id);
            removed = true;
        }
    }
    return removed;
}

bool GridModel::rotateItem(int id, QString* reason)
{
    Item* item = findItem(id);
    if (!item) {
        if (reason) {
            *reason = "No item is selected.";
        }
        return false;
    }
    if (!isRotatable(item->kind)) {
        if (reason) {
            *reason = "This item cannot be rotated.";
        }
        return false;
    }
    const int nextRotation = (item->rotationQuarters + 1) % 4;
    if (!canPlace(item->kind, item->anchor, item->layer, nextRotation, item->id, reason)) {
        return false;
    }
    item->rotationQuarters = nextRotation;
    m_dirty = true;
    invalidateIndexes();
    return true;
}

void GridModel::remapMusicMatRotationsForMapNorth(Direction oldMapNorth, Direction newMapNorth)
{
    if (oldMapNorth == newMapNorth) {
        return;
    }
    for (Item& item : m_items) {
        if (!isMusicMat(item.kind)) {
            continue;
        }
        item.rotationQuarters = remapScreenRotationForMapNorthChange(
            item.rotationQuarters, oldMapNorth, newMapNorth);
    }
    m_dirty = true;
    invalidateIndexes();
}

bool GridModel::moveItem(int id, const QPoint& newAnchor, QString* reason)
{
    Item* item = findItem(id);
    if (!item) {
        if (reason) {
            *reason = "No item is selected.";
        }
        return false;
    }
    if (item->anchor == newAnchor) {
        return true;
    }
    if (!canPlace(item->kind, newAnchor, item->layer, item->rotationQuarters, item->id, reason)) {
        return false;
    }
    item->anchor = newAnchor;
    m_dirty = true;
    invalidateIndexes();
    return true;
}

bool GridModel::moveItems(const QVector<int>& ids, const QPoint& delta, QString* reason)
{
    if (ids.isEmpty() || delta.isNull()) {
        return true;
    }

    QVector<Item> movedItems = m_items;
    bool changed = false;
    for (Item& item : movedItems) {
        if (ids.contains(item.id)) {
            item.anchor += delta;
            changed = true;
        }
    }

    if (!changed) {
        if (reason) {
            *reason = "No item is selected.";
        }
        return false;
    }

    if (!buildModelFromItems(movedItems, reason)) {
        return false;
    }

    m_items = movedItems;
    m_dirty = true;
    invalidateIndexes();
    return true;
}

QVector<int> GridModel::itemIdsInRect(const QRect& rect, int layer) const
{
    QVector<int> ids;
    const QRect normalized(
        QPoint(std::min(rect.left(), rect.right()), std::min(rect.top(), rect.bottom())),
        QPoint(std::max(rect.left(), rect.right()), std::max(rect.top(), rect.bottom())));
    for (const Item& item : m_items) {
        if (item.layer != layer) {
            continue;
        }
        for (const QPoint& cell : occupiedCells(item)) {
            if (normalized.contains(cell)) {
                ids.push_back(item.id);
                break;
            }
        }
    }
    return ids;
}

QRect GridModel::contentBounds(int layer) const
{
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();
    bool hasCells = false;
    for (const Item& item : m_items) {
        if (item.layer != layer && item.layer != layer - 1) {
            continue;
        }
        for (const QPoint& cell : occupiedCells(item)) {
            minX = std::min(minX, cell.x());
            minY = std::min(minY, cell.y());
            maxX = std::max(maxX, cell.x());
            maxY = std::max(maxY, cell.y());
            hasCells = true;
        }
    }
    if (!hasCells) {
        return QRect(0, 0, 1, 1);
    }
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

QRect GridModel::visibleBounds(int layer) const
{
    QRect bounds = contentBounds(layer).adjusted(-kGridMarginCells, -kGridMarginCells, kGridMarginCells, kGridMarginCells);
    if (bounds.width() < kMinimumGridCells) {
        const int missing = kMinimumGridCells - bounds.width();
        bounds.adjust(-(missing / 2), 0, missing - (missing / 2), 0);
    }
    if (bounds.height() < kMinimumGridCells) {
        const int missing = kMinimumGridCells - bounds.height();
        bounds.adjust(0, -(missing / 2), 0, missing - (missing / 2));
    }
    return bounds;
}

int GridModel::maxLayer() const
{
    int result = 0;
    for (const Item& item : m_items) {
        result = std::max(result, item.layer);
    }
    return result;
}

void GridModel::shiftLayersUp(int fromLayer)
{
    for (Item& item : m_items) {
        if (item.layer >= fromLayer) {
            ++item.layer;
        }
    }
    m_dirty = true;
    invalidateIndexes();
}

QVector<LayerCell> GridModel::railNeighbors(const LayerCell& cell) const
{
    QVector<LayerCell> neighbors;
    for (Direction direction : {Direction::North, Direction::East, Direction::South, Direction::West}) {
        const QPoint next = cell.cell + directionDelta(direction);
        for (int layerDelta : {0, -1, 1}) {
            const int nextLayer = cell.layer + layerDelta;
            if (nextLayer < 0) {
                continue;
            }
            if (hasRailAt(next, nextLayer)) {
                neighbors.push_back({next, nextLayer});
            }
        }
    }
    return neighbors;
}

int GridModel::railDegree(const LayerCell& cell) const
{
    return railNeighbors(cell).size();
}

QVector<LayerCell> GridModel::tJunctionCells() const
{
    QVector<LayerCell> cells;
    for (const Item& item : m_items) {
        const LayerCell cell{item.anchor, item.layer};
        if (item.kind == ItemKind::Rail && railDegree(cell) == 3) {
            cells.push_back(cell);
        }
    }
    return cells;
}

bool GridModel::hasTJunction() const
{
    return !tJunctionCells().isEmpty();
}

std::optional<LayerCell> GridModel::firstHandcarCell() const
{
    for (const Item& item : m_items) {
        if (item.kind == ItemKind::Handcar) {
            return LayerCell{item.anchor, item.layer};
        }
    }
    return std::nullopt;
}

QVector<const Item*> GridModel::laserSensors() const
{
    ensureIndexes();
    return itemsFromIds(m_laserIds);
}

QString GridModel::playValidationMessage(Direction initialDirection) const
{
    const auto handcar = firstHandcarCell();
    if (!handcar.has_value()) {
        return "Add a handcar to a railway track before playing.";
    }
    if (hasTJunction()) {
        return "Play is disabled while railway T-junctions are present.";
    }
    if (!hasRailAt(handcar->cell, handcar->layer)) {
        return "The handcar is not on a railway track.";
    }
    const QPoint preferredCell = handcar->cell + directionDelta(initialDirection);
    const QVector<LayerCell> neighbors = railNeighbors(*handcar);
    const auto hasPreferredNeighbor = std::any_of(neighbors.cbegin(), neighbors.cend(), [&](const LayerCell& neighbor) {
        return neighbor.cell == preferredCell;
    });
    if (!hasPreferredNeighbor) {
        return "The selected initial direction does not lead to a railway track.";
    }
    return {};
}

QVector<int> GridModel::headOnLaserIds(Direction initialDirection) const
{
    const auto handcar = firstHandcarCell();
    if (!handcar.has_value() || hasTJunction()) {
        return {};
    }

    // Simulate the cart path and collect lasers triggered head-on.
    // Head-on = cart enters a beam cell while moving opposite to the laser's beam direction.
    const int kMaxSteps = 10000;
    QVector<LayerCell> visited;
    QVector<int> result;

    LayerCell current = *handcar;
    std::optional<LayerCell> previous = std::nullopt;
    visited.push_back(current);

    for (int step = 0; step < kMaxSteps; ++step) {
        // Determine next cell (mirrors MainWindow::nextRailCell logic)
        QVector<LayerCell> neighbors = railNeighbors(current);
        if (neighbors.isEmpty()) {
            break;
        }

        LayerCell next{QPoint{}, -1};
        bool found = false;

        if (!previous.has_value()) {
            const QPoint preferred = current.cell + directionDelta(initialDirection);
            for (const LayerCell& nb : neighbors) {
                if (nb.cell == preferred && nb.layer == current.layer) { next = nb; found = true; break; }
            }
            if (!found) {
                for (const LayerCell& nb : neighbors) {
                    if (nb.cell == preferred) { next = nb; found = true; break; }
                }
            }
        } else {
            QVector<LayerCell> candidates = neighbors;
            candidates.erase(std::remove(candidates.begin(), candidates.end(), *previous), candidates.end());
            if (candidates.isEmpty()) {
                break;
            }
            const QPoint straight = current.cell + (current.cell - previous->cell);
            for (const LayerCell& nb : candidates) {
                if (nb.cell == straight && nb.layer == current.layer) { next = nb; found = true; break; }
            }
            if (!found) {
                for (const LayerCell& nb : candidates) {
                    if (nb.cell == straight) { next = nb; found = true; break; }
                }
            }
            if (!found) {
                for (Direction dir : {Direction::North, Direction::East, Direction::South, Direction::West}) {
                    const QPoint candidate = current.cell + directionDelta(dir);
                    for (const LayerCell& nb : candidates) {
                        if (nb.cell == candidate && nb.layer == current.layer) { next = nb; found = true; break; }
                    }
                    if (found) break;
                    for (const LayerCell& nb : candidates) {
                        if (nb.cell == candidate) { next = nb; found = true; break; }
                    }
                    if (found) break;
                }
            }
            if (!found) {
                next = candidates.front();
                found = true;
            }
        }

        if (!found) {
            break;
        }

        // Loop detection: stop if we've been to this cell before
        if (visited.contains(next)) {
            break;
        }

        previous = current;
        current = next;
        visited.push_back(current);

        // Check all lasers: is this step head-on into a beam?
        const QPoint cartDelta = current.cell - previous->cell;
        for (const Item* laser : laserSensors()) {
            if (result.contains(laser->id)) {
                continue;
            }
            const QVector<LayerCell> beam = laserBeamCells(*laser);
            if (!beam.contains(current)) {
                continue;
            }
            // Head-on: cart moves opposite to laser's beam direction
            const QPoint beamDelta = directionDelta(directionFromRotation(laser->rotationQuarters));
            if (cartDelta.x() == -beamDelta.x() && cartDelta.y() == -beamDelta.y()) {
                result.push_back(laser->id);
            }
        }
    }

    return result;
}

bool GridModel::buildModelFromItems(const QVector<Item>& items, QString* reason) const
{
    GridModel testModel;
    auto addItems = [&](bool handcars) {
        for (const Item& item : items) {
            if ((item.kind == ItemKind::Handcar) != handcars) {
                continue;
            }
            QString localReason;
            if (!testModel.addItem(item.kind, item.anchor, item.layer, item.rotationQuarters, nullptr, &localReason)) {
                if (reason) {
                    *reason = QString("Invalid placement for %1: %2").arg(itemDisplayName(item.kind), localReason);
                }
                return false;
            }
        }
        return true;
    };

    return addItems(false) && addItems(true);
}
