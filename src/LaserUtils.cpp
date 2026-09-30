#include "LaserUtils.h"

QVector<LayerCell> laserBeamCells(const Item& laser)
{
    const QPoint delta = directionDelta(directionFromRotation(laser.rotationQuarters));
    return {
        {laser.anchor + delta, laser.layer},
        {laser.anchor + QPoint(delta.x() * 2, delta.y() * 2), laser.layer},
    };
}

QVector<LayerCell> laserActivationCells(const Item& laser)
{
    QVector<LayerCell> cells;
    for (int layerDelta : {-1, 0, 1}) {
        const int targetLayer = laser.layer + layerDelta;
        if (targetLayer < 0) {
            continue;
        }
        cells.push_back({laser.anchor + directionDelta(Direction::North), targetLayer});
        cells.push_back({laser.anchor + directionDelta(Direction::East), targetLayer});
        cells.push_back({laser.anchor + directionDelta(Direction::South), targetLayer});
        cells.push_back({laser.anchor + directionDelta(Direction::West), targetLayer});
        if (layerDelta != 0) {
            cells.push_back({laser.anchor, targetLayer});
        }
    }
    return uniqueLayerCells(cells);
}

QVector<LayerCell> laserSoundTargetCells(const Item& laser)
{
    QVector<LayerCell> targets;
    const QVector<LayerCell> beam = laserBeamCells(laser);
    const QPoint beamAdjacentCell = laser.anchor + directionDelta(directionFromRotation(laser.rotationQuarters));
    for (const LayerCell& cell : laserActivationCells(laser)) {
        if (!beam.contains(cell) && cell.cell != beamAdjacentCell) {
            targets.push_back(cell);
        }
    }
    return uniqueLayerCells(targets);
}
