#pragma once

#include "ItemTypes.h"

#include <QtCore/QPoint>
#include <QtCore/QVector>

QVector<LayerCell> laserBeamCells(const Item& laser);
QVector<LayerCell> laserActivationCells(const Item& laser);
QVector<LayerCell> laserSoundTargetCells(const Item& laser);
