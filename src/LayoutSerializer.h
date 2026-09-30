#pragma once

#include "AppConstants.h"
#include "ItemTypes.h"

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QVector>

class GridModel;

struct TrackLayout {
    int msPerTile = kDefaultMsPerTile;
    Direction initialDirection = Direction::North;
    Direction mapNorth = Direction::North;
    QVector<Item> items;
};

QJsonObject layoutToJson(const TrackLayout& layout);
bool layoutFromJson(const QJsonObject& root, TrackLayout* layout, QString* error);

// Reconstrói um GridModel validando cada item (handcars por último).
bool buildGridModelFromLayout(const TrackLayout& layout, GridModel* model, QString* error);

bool saveLayoutFile(const TrackLayout& layout, const QString& path, QString* error);
bool loadLayoutFile(const QString& path, TrackLayout* layout, QString* error);
