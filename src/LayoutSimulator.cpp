#include "LayoutSimulator.h"

#include "LaserUtils.h"

#include <QtCore/QSet>

#include <algorithm>

namespace LayoutSimulation {

namespace {

quint64 stateKey(const LayerCell& current, const std::optional<LayerCell>& previous)
{
    auto pack = [](const LayerCell& cell) -> quint64 {
        const quint64 x = static_cast<quint64>(static_cast<quint32>(cell.cell.x() + 0x8000));
        const quint64 y = static_cast<quint64>(static_cast<quint32>(cell.cell.y() + 0x8000));
        const quint64 layer = static_cast<quint64>(static_cast<quint32>(cell.layer));
        return (x & 0xFFFF) | ((y & 0xFFFF) << 16) | ((layer & 0xFF) << 32);
    };
    quint64 key = pack(current);
    if (previous.has_value()) {
        key ^= pack(*previous) << 20;
        key |= quint64(1) << 62;
    }
    return key;
}

void fireLaser(
    const GridModel& model,
    const Item& laser,
    QSet<int>& firedLaserIds,
    QSet<int>& triggeredPinwheelIds,
    const std::function<void(const Item&)>& onSoundItem);

void triggerPinwheel(
    const GridModel& model,
    const Item& pinwheel,
    QSet<int>& firedLaserIds,
    QSet<int>& triggeredPinwheelIds,
    const std::function<void(const Item&)>& onSoundItem)
{
    if (triggeredPinwheelIds.contains(pinwheel.id)) {
        return;
    }
    triggeredPinwheelIds.insert(pinwheel.id);

    const LayerCell pinwheelCell{pinwheel.anchor, pinwheel.layer};
    for (const Item* laser : model.laserSensors()) {
        if (!laserBeamCells(*laser).contains(pinwheelCell)) {
            continue;
        }
        fireLaser(model, *laser, firedLaserIds, triggeredPinwheelIds, onSoundItem);
    }
}

void fireLaser(
    const GridModel& model,
    const Item& laser,
    QSet<int>& firedLaserIds,
    QSet<int>& triggeredPinwheelIds,
    const std::function<void(const Item&)>& onSoundItem)
{
    if (firedLaserIds.contains(laser.id)) {
        return;
    }
    firedLaserIds.insert(laser.id);

    QVector<int> playedIds;
    for (const LayerCell& target : laserSoundTargetCells(laser)) {
        const QVector<const Item*> items = model.itemsOccupying(target.cell, target.layer);
        for (const Item* item : items) {
            if (isSoundItem(item->kind)) {
                if (playedIds.contains(item->id)) {
                    continue;
                }
                playedIds.push_back(item->id);
                if (onSoundItem) {
                    onSoundItem(*item);
                }
            } else if (isPinwheel(item->kind)) {
                triggerPinwheel(model, *item, firedLaserIds, triggeredPinwheelIds, onSoundItem);
            }
        }
    }
}

} // namespace

std::optional<LayerCell> nextRailCell(
    const GridModel& model,
    const LayerCell& current,
    const std::optional<LayerCell>& previous,
    Direction initialDirection)
{
    QVector<LayerCell> neighbors = model.railNeighbors(current);
    if (neighbors.isEmpty()) {
        return std::nullopt;
    }

    if (!previous.has_value()) {
        const QPoint preferred = current.cell + directionDelta(initialDirection);
        for (const LayerCell& neighbor : neighbors) {
            if (neighbor.cell == preferred && neighbor.layer == current.layer) {
                return neighbor;
            }
        }
        for (const LayerCell& neighbor : neighbors) {
            if (neighbor.cell == preferred) {
                return neighbor;
            }
        }
        return std::nullopt;
    }

    neighbors.erase(std::remove(neighbors.begin(), neighbors.end(), *previous), neighbors.end());
    if (neighbors.isEmpty()) {
        return std::nullopt;
    }

    const QPoint straight = current.cell + (current.cell - previous->cell);
    for (const LayerCell& neighbor : neighbors) {
        if (neighbor.cell == straight && neighbor.layer == current.layer) {
            return neighbor;
        }
    }
    for (const LayerCell& neighbor : neighbors) {
        if (neighbor.cell == straight) {
            return neighbor;
        }
    }

    for (Direction direction : {Direction::North, Direction::East, Direction::South, Direction::West}) {
        const QPoint candidate = current.cell + directionDelta(direction);
        for (const LayerCell& neighbor : neighbors) {
            if (neighbor.cell == candidate && neighbor.layer == current.layer) {
                return neighbor;
            }
        }
        for (const LayerCell& neighbor : neighbors) {
            if (neighbor.cell == candidate) {
                return neighbor;
            }
        }
    }
    return neighbors.front();
}

void triggerLasersForCart(
    const GridModel& model,
    const LayerCell& cartCell,
    QVector<int>& activeLaserIds,
    const std::function<void(const Item&)>& onSoundItem)
{
    QVector<int> newActiveIds;
    QSet<int> firedLaserIds;
    QSet<int> triggeredPinwheelIds;
    for (const Item* laser : model.laserSensors()) {
        const QVector<LayerCell> beam = laserBeamCells(*laser);
        if (!beam.contains(cartCell) && !(laser->anchor == cartCell.cell && laser->layer == cartCell.layer)) {
            continue;
        }
        newActiveIds.push_back(laser->id);
        if (activeLaserIds.contains(laser->id)) {
            continue;
        }
        fireLaser(model, *laser, firedLaserIds, triggeredPinwheelIds, onSoundItem);
    }
    activeLaserIds = newActiveIds;
}

Result run(const GridModel& model, Direction initialDirection, int maxSteps)
{
    Result result;

    const QString validation = model.playValidationMessage(initialDirection);
    if (!validation.isEmpty()) {
        result.error = validation;
        return result;
    }

    LayerCell current = *model.firstHandcarCell();
    std::optional<LayerCell> previous;
    QVector<int> activeLaserIds;
    QSet<quint64> visitedStates;

    result.cartCells.push_back(current);
    visitedStates.insert(stateKey(current, previous));

    int step = -1;
    auto record = [&](const Item& item) {
        result.events.push_back({step, item.id});
    };

    triggerLasersForCart(model, current, activeLaserIds, record);

    for (int i = 0; i < maxSteps; ++i) {
        const std::optional<LayerCell> next = nextRailCell(model, current, previous, initialDirection);
        if (!next.has_value()) {
            result.endedAtDeadEnd = true;
            return result;
        }
        const quint64 key = stateKey(*next, current);
        if (visitedStates.contains(key)) {
            result.loopClosed = true;
            return result;
        }
        visitedStates.insert(key);

        previous = current;
        current = *next;
        ++step;
        ++result.stepsTaken;
        result.cartCells.push_back(current);
        triggerLasersForCart(model, current, activeLaserIds, record);
    }

    result.error = "Simulation exceeded the maximum number of steps.";
    return result;
}

} // namespace LayoutSimulation
