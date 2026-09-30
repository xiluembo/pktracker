#include "ItemTypes.h"

#include <QtCore/QPair>
#include <QtCore/QStringList>

#include <cmath>

bool operator==(const LayerCell& a, const LayerCell& b)
{
    return a.cell == b.cell && a.layer == b.layer;
}

Direction directionFromRotation(int rotationQuarters)
{
    const int normalized = ((rotationQuarters % 4) + 4) % 4;
    return static_cast<Direction>(normalized);
}

int directionToRotation(Direction direction)
{
    return static_cast<int>(direction);
}

int normalizedRotationQuarters(int rotationQuarters)
{
    return ((rotationQuarters % 4) + 4) % 4;
}

Direction musicalDirectionFromScreen(int screenRotationQuarters, Direction mapNorth)
{
    const int screen = normalizedRotationQuarters(screenRotationQuarters);
    const int north = directionToRotation(mapNorth);
    return directionFromRotation(screen - north);
}

int screenRotationFromMusical(Direction musicalDirection, Direction mapNorth)
{
    return normalizedRotationQuarters(directionToRotation(musicalDirection) + directionToRotation(mapNorth));
}

int remapScreenRotationForMapNorthChange(int screenRotationQuarters, Direction oldMapNorth, Direction newMapNorth)
{
    const Direction musical = musicalDirectionFromScreen(screenRotationQuarters, oldMapNorth);
    return screenRotationFromMusical(musical, newMapNorth);
}

QPoint directionDelta(Direction direction)
{
    switch (direction) {
    case Direction::North:
        return {0, -1};
    case Direction::East:
        return {1, 0};
    case Direction::South:
        return {0, 1};
    case Direction::West:
        return {-1, 0};
    }
    return {0, 0};
}

QString directionName(Direction direction)
{
    switch (direction) {
    case Direction::North:
        return "North";
    case Direction::East:
        return "East";
    case Direction::South:
        return "South";
    case Direction::West:
        return "West";
    }
    return "North";
}

std::optional<Direction> directionFromName(const QString& text)
{
    if (text == "North") {
        return Direction::North;
    }
    if (text == "East") {
        return Direction::East;
    }
    if (text == "South") {
        return Direction::South;
    }
    if (text == "West") {
        return Direction::West;
    }
    return std::nullopt;
}

bool isMusicMat(ItemKind kind)
{
    return kind >= ItemKind::MusicLowDo && kind <= ItemKind::MusicHighDo;
}

bool isPercussion(ItemKind kind)
{
    return kind == ItemKind::BooBox || kind == ItemKind::BigDrum;
}

bool isPinwheel(ItemKind kind)
{
    return kind == ItemKind::Pinwheel;
}

bool isSoundItem(ItemKind kind)
{
    return isMusicMat(kind) || isPercussion(kind);
}

bool isRotatable(ItemKind kind)
{
    return isMusicMat(kind) || kind == ItemKind::BigDrum || kind == ItemKind::Laser;
}

QString itemKindKey(ItemKind kind)
{
    switch (kind) {
    case ItemKind::MusicLowDo:
        return "music_low_do";
    case ItemKind::MusicRe:
        return "music_re";
    case ItemKind::MusicMi:
        return "music_mi";
    case ItemKind::MusicFa:
        return "music_fa";
    case ItemKind::MusicSol:
        return "music_sol";
    case ItemKind::MusicLa:
        return "music_la";
    case ItemKind::MusicTi:
        return "music_ti";
    case ItemKind::MusicHighDo:
        return "music_high_do";
    case ItemKind::BooBox:
        return "boo_in_the_box";
    case ItemKind::BigDrum:
        return "big_drum";
    case ItemKind::Rail:
        return "railway_track";
    case ItemKind::Handcar:
        return "handcar";
    case ItemKind::Laser:
        return "laser_sensor";
    case ItemKind::Pinwheel:
        return "pinwheel";
    }
    return "railway_track";
}

std::optional<ItemKind> itemKindFromKey(const QString& key)
{
    static const QVector<QPair<QString, ItemKind>> entries = {
        {"music_low_do", ItemKind::MusicLowDo},
        {"music_re", ItemKind::MusicRe},
        {"music_mi", ItemKind::MusicMi},
        {"music_fa", ItemKind::MusicFa},
        {"music_sol", ItemKind::MusicSol},
        {"music_la", ItemKind::MusicLa},
        {"music_ti", ItemKind::MusicTi},
        {"music_high_do", ItemKind::MusicHighDo},
        {"boo_in_the_box", ItemKind::BooBox},
        {"big_drum", ItemKind::BigDrum},
        {"railway_track", ItemKind::Rail},
        {"handcar", ItemKind::Handcar},
        {"laser_sensor", ItemKind::Laser},
        {"pinwheel", ItemKind::Pinwheel},
    };
    for (const auto& entry : entries) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    return std::nullopt;
}

QString itemDisplayName(ItemKind kind)
{
    switch (kind) {
    case ItemKind::MusicLowDo:
        return "Music mat (Low Do)";
    case ItemKind::MusicRe:
        return "Music mat (Re)";
    case ItemKind::MusicMi:
        return "Music mat (Mi)";
    case ItemKind::MusicFa:
        return "Music mat (Fa)";
    case ItemKind::MusicSol:
        return "Music mat (Sol)";
    case ItemKind::MusicLa:
        return "Music mat (La)";
    case ItemKind::MusicTi:
        return "Music mat (Ti)";
    case ItemKind::MusicHighDo:
        return "Music mat (High Do)";
    case ItemKind::BooBox:
        return "Boo-in-the-box";
    case ItemKind::BigDrum:
        return "Big drum";
    case ItemKind::Rail:
        return "Railway track";
    case ItemKind::Handcar:
        return "Handcar";
    case ItemKind::Laser:
        return "Laser sensor";
    case ItemKind::Pinwheel:
        return "Pinwheel";
    }
    return "Item";
}

QColor musicMatColor(ItemKind kind)
{
    switch (kind) {
    case ItemKind::MusicLowDo:
        return QColor("#ef476f");
    case ItemKind::MusicRe:
        return QColor("#f9844a");
    case ItemKind::MusicMi:
        return QColor("#ffd166");
    case ItemKind::MusicFa:
        return QColor("#caff70");
    case ItemKind::MusicSol:
        return QColor("#63d471");
    case ItemKind::MusicLa:
        return QColor("#72ddf7");
    case ItemKind::MusicTi:
        return QColor("#4d96ff");
    case ItemKind::MusicHighDo:
        return QColor("#ff70c8");
    default:
        return QColor("#dddddd");
    }
}

int naturalLowMidi(ItemKind kind)
{
    switch (kind) {
    case ItemKind::MusicLowDo:
        return 48; // C3
    case ItemKind::MusicRe:
        return 50; // D3
    case ItemKind::MusicMi:
        return 52; // E3
    case ItemKind::MusicFa:
        return 53; // F3
    case ItemKind::MusicSol:
        return 55; // G3
    case ItemKind::MusicLa:
        return 57; // A3
    case ItemKind::MusicTi:
        return 59; // B3
    case ItemKind::MusicHighDo:
        return 60; // C4
    default:
        return 48;
    }
}

int playedMidi(ItemKind kind, Direction facing)
{
    const int base = naturalLowMidi(kind);
    switch (facing) {
    case Direction::North:
        return base;
    case Direction::South:
        return base + 12;
    case Direction::West:
        return base + 1;
    case Direction::East:
        return base + 13;
    }
    return base;
}

int playedMidiForItem(ItemKind kind, int screenRotationQuarters, Direction mapNorth)
{
    return playedMidi(kind, musicalDirectionFromScreen(screenRotationQuarters, mapNorth));
}

QString enharmonicHint(ItemKind kind, Direction facing)
{
    if (kind == ItemKind::MusicFa && facing == Direction::North) {
        return "Same pitch as Music mat (Mi) facing West.";
    }
    if (kind == ItemKind::MusicMi && facing == Direction::West) {
        return "Same pitch as Music mat (Fa) facing North.";
    }
    if (kind == ItemKind::MusicLowDo && facing == Direction::South) {
        return "Same pitch as Music mat (Ti) facing West and Music mat (High Do) facing North.";
    }
    if (kind == ItemKind::MusicTi && facing == Direction::West) {
        return "Same pitch as Music mat (Low Do) facing South and Music mat (High Do) facing North.";
    }
    if (kind == ItemKind::MusicHighDo && facing == Direction::North) {
        return "Same pitch as Music mat (Low Do) facing South and Music mat (Ti) facing West.";
    }
    return {};
}

double midiToFrequency(int midi)
{
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

QString pitchName(int midi)
{
    static const QStringList names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int index = ((midi % 12) + 12) % 12;
    const int octave = (midi / 12) - 1;
    return names[index] + QString::number(octave);
}

QVector<QPoint> uniqueCells(QVector<QPoint> cells)
{
    QVector<QPoint> result;
    for (const QPoint& cell : cells) {
        if (!result.contains(cell)) {
            result.push_back(cell);
        }
    }
    return result;
}

QVector<LayerCell> uniqueLayerCells(QVector<LayerCell> cells)
{
    QVector<LayerCell> result;
    for (const LayerCell& cell : cells) {
        if (!result.contains(cell)) {
            result.push_back(cell);
        }
    }
    return result;
}
