#include "LayoutSerializer.h"

#include "GridModel.h"

#include <QtCore/QFile>
#include <QtCore/QIODevice>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QSaveFile>

#include <algorithm>

QJsonObject layoutToJson(const TrackLayout& layout)
{
    QJsonObject root;
    root["format"] = "pktracks-layout";
    root["formatVersion"] = 1;
    root["msPerTile"] = layout.msPerTile;
    root["initialDirection"] = directionName(layout.initialDirection);
    root["mapNorth"] = directionName(layout.mapNorth);

    QJsonArray items;
    for (const Item& item : layout.items) {
        QJsonObject object;
        object["kind"] = itemKindKey(item.kind);
        object["rotationQuarters"] = item.rotationQuarters;
        QJsonObject anchor;
        anchor["col"] = item.anchor.x();
        anchor["row"] = item.anchor.y();
        object["anchor"] = anchor;
        if (item.layer != 0) {
            object["layer"] = item.layer;
        }
        items.push_back(object);
    }
    root["items"] = items;
    return root;
}

bool layoutFromJson(const QJsonObject& root, TrackLayout* layout, QString* error)
{
    if (root.value("format").toString() != "pktracks-layout") {
        if (error) {
            *error = "This file is not a Pokopia track layout.";
        }
        return false;
    }
    if (root.value("formatVersion").toInt() != 1) {
        if (error) {
            *error = "Unsupported layout format version.";
        }
        return false;
    }
    if (!root.value("items").isArray()) {
        if (error) {
            *error = "The layout does not contain an item list.";
        }
        return false;
    }

    TrackLayout parsed;
    const QJsonArray items = root.value("items").toArray();
    for (const QJsonValue& value : items) {
        if (!value.isObject()) {
            if (error) {
                *error = "An item entry is not an object.";
            }
            return false;
        }
        const QJsonObject object = value.toObject();
        const auto kind = itemKindFromKey(object.value("kind").toString());
        if (!kind.has_value()) {
            if (error) {
                *error = "The layout contains an unknown item kind.";
            }
            return false;
        }
        const QJsonObject anchor = object.value("anchor").toObject();
        const int layer = std::max(0, object.value("layer").toInt(0));
        parsed.items.push_back(Item{
            0,
            *kind,
            QPoint(anchor.value("col").toInt(), anchor.value("row").toInt()),
            normalizedRotationQuarters(object.value("rotationQuarters").toInt()),
            layer,
        });
    }

    parsed.msPerTile = root.value("msPerTile").toInt(kDefaultMsPerTile);
    const auto direction = directionFromName(root.value("initialDirection").toString("North"));
    parsed.initialDirection = direction.value_or(Direction::North);
    const auto mapNorth = directionFromName(root.value("mapNorth").toString("North"));
    parsed.mapNorth = mapNorth.value_or(Direction::North);

    *layout = parsed;
    return true;
}

bool buildGridModelFromLayout(const TrackLayout& layout, GridModel* model, QString* error)
{
    GridModel nextModel;
    auto addParsed = [&](bool handcars) {
        for (const Item& item : layout.items) {
            if ((item.kind == ItemKind::Handcar) != handcars) {
                continue;
            }
            QString reason;
            if (!nextModel.addItem(item.kind, item.anchor, item.layer, item.rotationQuarters, nullptr, &reason)) {
                if (error) {
                    *error = QString("Invalid item placement for %1: %2").arg(itemDisplayName(item.kind), reason);
                }
                return false;
            }
        }
        return true;
    };

    if (!addParsed(false) || !addParsed(true)) {
        return false;
    }

    nextModel.setDirty(false);
    *model = nextModel;
    return true;
}

bool saveLayoutFile(const TrackLayout& layout, const QString& path, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = "Could not open the file for writing.";
        }
        return false;
    }
    file.write(QJsonDocument(layoutToJson(layout)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) {
            *error = "Could not save the file.";
        }
        return false;
    }
    return true;
}

bool loadLayoutFile(const QString& path, TrackLayout* layout, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = "Could not open the file.";
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = "The file is not valid JSON.";
        }
        return false;
    }

    return layoutFromJson(document.object(), layout, error);
}
