#pragma once

#include <QtCore/QHashFunctions>
#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtGui/QColor>

#include <optional>

enum class Direction {
    North = 0,
    East = 1,
    South = 2,
    West = 3,
};

enum class ItemKind {
    MusicLowDo,
    MusicRe,
    MusicMi,
    MusicFa,
    MusicSol,
    MusicLa,
    MusicTi,
    MusicHighDo,
    BooBox,
    BigDrum,
    Rail,
    Handcar,
    Laser,
    Pinwheel,
};

enum class PercussionKind {
    BooBox,
    BigDrum,
};

struct Item {
    int id = 0;
    ItemKind kind = ItemKind::Rail;
    QPoint anchor;
    int rotationQuarters = 0;
    int layer = 0;
};

struct LayerCell {
    QPoint cell;
    int layer = 0;
};

bool operator==(const LayerCell& a, const LayerCell& b);

inline size_t qHash(const LayerCell& cell, size_t seed = 0)
{
    return qHashMulti(seed, cell.cell.x(), cell.cell.y(), cell.layer);
}

Direction directionFromRotation(int rotationQuarters);
int directionToRotation(Direction direction);
int normalizedRotationQuarters(int rotationQuarters);
Direction musicalDirectionFromScreen(int screenRotationQuarters, Direction mapNorth);
int screenRotationFromMusical(Direction musicalDirection, Direction mapNorth);
int remapScreenRotationForMapNorthChange(int screenRotationQuarters, Direction oldMapNorth, Direction newMapNorth);
QPoint directionDelta(Direction direction);
QString directionName(Direction direction);
std::optional<Direction> directionFromName(const QString& text);

bool isMusicMat(ItemKind kind);
bool isPercussion(ItemKind kind);
bool isPinwheel(ItemKind kind);
bool isSoundItem(ItemKind kind);
bool isRotatable(ItemKind kind);

QString itemKindKey(ItemKind kind);
std::optional<ItemKind> itemKindFromKey(const QString& key);
QString itemDisplayName(ItemKind kind);
QColor musicMatColor(ItemKind kind);

int naturalLowMidi(ItemKind kind);
int playedMidi(ItemKind kind, Direction facing);
int playedMidiForItem(ItemKind kind, int screenRotationQuarters, Direction mapNorth);
QString enharmonicHint(ItemKind kind, Direction facing);
double midiToFrequency(int midi);
QString pitchName(int midi);
QVector<QPoint> uniqueCells(QVector<QPoint> cells);
QVector<LayerCell> uniqueLayerCells(QVector<LayerCell> cells);
