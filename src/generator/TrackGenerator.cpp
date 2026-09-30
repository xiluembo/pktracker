#include "TrackGenerator.h"

#include "GridModel.h"
#include "LaserUtils.h"
#include "LayoutSimulator.h"

#include <QtCore/QHash>
#include <QtCore/QSet>

#include <algorithm>
#include <climits>
#include <cmath>

namespace {

quint64 packCell(const QPoint& cell)
{
    return (static_cast<quint64>(static_cast<quint32>(cell.x())) << 32)
        | static_cast<quint64>(static_cast<quint32>(cell.y()));
}

quint64 packLayerCell(const QPoint& cell, int layer)
{
    return packCell(cell) ^ (static_cast<quint64>(static_cast<quint32>(layer)) * 0x9E3779B97F4A7C15ULL);
}

int rotationForDelta(const QPoint& delta)
{
    if (delta == QPoint(0, -1)) {
        return 0;
    }
    if (delta == QPoint(1, 0)) {
        return 1;
    }
    if (delta == QPoint(0, 1)) {
        return 2;
    }
    return 3;
}

Direction directionForDelta(const QPoint& delta)
{
    return directionFromRotation(rotationForDelta(delta));
}

struct PlannedItem {
    ItemKind kind = ItemKind::Rail;
    int rotationQuarters = 0;
    QPoint cell;
    int layer = 0;
};

// Réplica de GridModel::occupiedLayerCells para itens ainda não inseridos.
QVector<LayerCell> plannedOccupiedCells(const PlannedItem& item)
{
    QVector<QPoint> cells{item.cell};
    if (item.kind == ItemKind::BigDrum) {
        cells.push_back(item.cell + (item.rotationQuarters % 2 == 0 ? QPoint(0, 1) : QPoint(1, 0)));
    }
    QVector<LayerCell> layerCells;
    for (const QPoint& cell : cells) {
        layerCells.push_back({cell, item.layer});
        if (isPercussion(item.kind)) {
            layerCells.push_back({cell, item.layer + 1});
        }
    }
    return layerCells;
}

Item toItem(const PlannedItem& planned)
{
    return Item{0, planned.kind, planned.cell, planned.rotationQuarters, planned.layer};
}

struct SidePlan {
    bool ok = false;
    QString failReason;
    QVector<PlannedItem> lasers;
    QVector<PlannedItem> pinwheels;
    QVector<QPair<PlannedItem, StepNote>> soundItems;
    QVector<QPair<StepNote, QString>> unplaced;
};

struct Slot {
    QPoint cell;
    int layer = 0;
    bool isBehind = false;
};

// Distribui notas nos slots de um elo. Percussão prefere os slots L1 (ela
// também ocupa a camada de cima); Big drum exige slot "atrás" para a extensão
// apontar para fora. Deixa em `queue` o que não coube.
QVector<QPair<PlannedItem, StepNote>> assignNotesToSlots(
    QVector<Slot> slotList, QVector<StepNote>& queue, const QPoint& outward)
{
    QVector<QPair<PlannedItem, StepNote>> assigned;
    while (!slotList.isEmpty() && !queue.isEmpty()) {
        const Slot slot = slotList.takeFirst();

        int pick = -1;
        if (slot.layer == 0) {
            for (int i = 0; i < queue.size(); ++i) {
                if (!queue[i].percussion) {
                    pick = i;
                    break;
                }
            }
        } else {
            for (int i = 0; i < queue.size(); ++i) {
                if (queue[i].percussion && (queue[i].kind != ItemKind::BigDrum || slot.isBehind)) {
                    pick = i;
                    break;
                }
            }
        }
        if (pick < 0) {
            for (int i = 0; i < queue.size(); ++i) {
                if (queue[i].kind == ItemKind::BigDrum && !slot.isBehind) {
                    continue;
                }
                pick = i;
                break;
            }
        }
        if (pick < 0) {
            continue; // slot sem nota compatível (só Big drum para âncora)
        }

        const StepNote note = queue.takeAt(pick);
        PlannedItem item;
        item.layer = slot.layer;
        if (note.percussion) {
            item.kind = note.kind;
            if (note.kind == ItemKind::BigDrum) {
                if (outward.y() != 0) {
                    item.rotationQuarters = 0;
                    item.cell = outward.y() > 0 ? slot.cell : slot.cell + QPoint(0, -1);
                } else {
                    item.rotationQuarters = 1;
                    item.cell = outward.x() > 0 ? slot.cell : slot.cell + QPoint(-1, 0);
                }
            } else {
                item.rotationQuarters = 0;
                item.cell = slot.cell;
            }
            if (slot.layer == 0) {
                // percussão em L0 também ocupa L1: invalida o slot de cima
                slotList.erase(std::remove_if(slotList.begin(), slotList.end(), [&](const Slot& other) {
                                return other.cell == slot.cell && other.layer == 1;
                            }),
                    slotList.end());
            }
        } else {
            item.kind = note.kind;
            item.rotationQuarters = note.rotationQuarters;
            item.cell = slot.cell;
        }
        assigned.push_back({item, note});
    }
    return assigned;
}

// Aloca `notes` (ordem = prioridade) em slotList de um lado do trilho.
// Preferência: atrás L0, atrás L1, âncora L1; pinwheel só quando os três
// níveis do elo esgotam e ainda há profundidade de cadeia disponível.
SidePlan planSide(
    const QPoint& rail,
    const QPoint& outward,
    const QVector<StepNote>& notes,
    bool anchorsOk,
    int maxPinwheelDepth)
{
    SidePlan plan;
    QVector<StepNote> queue = notes;
    const int laserRotation = rotationForDelta(-outward);

    int link = 0;
    while (!queue.isEmpty()) {
        const QPoint laserCell = rail + outward * (3 * link + 1);
        const QPoint behindCell = rail + outward * (3 * link + 2);
        plan.lasers.push_back({ItemKind::Laser, laserRotation, laserCell, 0});

        QVector<Slot> slotList;
        slotList.push_back({behindCell, 0, true});
        slotList.push_back({behindCell, 1, true});
        if (anchorsOk) {
            slotList.push_back({laserCell, 1, false});
        }

        // Tenta primeiro esgotar a fila só com os slots do elo. Se sobrar
        // nota, o slot "atrás L0" precisa obrigatoriamente virar pinwheel,
        // senão o próximo laser da cadeia nunca dispara.
        QVector<StepNote> noChainQueue = queue;
        const auto noChainAssigned = assignNotesToSlots(slotList, noChainQueue, outward);
        if (noChainQueue.isEmpty()) {
            plan.soundItems += noChainAssigned;
            queue.clear();
            break;
        }
        if (link >= maxPinwheelDepth) {
            plan.failReason = "capacidade do lado excedida";
            return plan;
        }
        plan.pinwheels.push_back({ItemKind::Pinwheel, 0, behindCell, 0});
        slotList.removeFirst();
        plan.soundItems += assignNotesToSlots(slotList, queue, outward);
        ++link;
    }

    plan.ok = true;
    return plan;
}

struct PlacementContext {
    QSet<quint64> rails;                     // células de trilho (L0)
    QHash<quint64, int> railPathIndex;       // packCell -> índice no caminho
    QHash<quint64, int> occupied;            // packLayerCell -> índice de item
    QHash<quint64, int> soundCellStep;       // packLayerCell -> simStep esperado
    QHash<quint64, int> pinwheelCellStep;    // packLayerCell (L0) -> simStep
    QVector<QPair<Item, int>> placedLasers;  // laser + simStep
    QVector<Item> layoutItems;
    QHash<quint64, int> expectedStepByCell;  // packLayerCell da âncora -> simStep
};

bool laserReachesCells(const Item& laser, const QSet<quint64>& cells)
{
    for (const LayerCell& target : laserSoundTargetCells(laser)) {
        if (cells.contains(packLayerCell(target.cell, target.layer))) {
            return true;
        }
    }
    return false;
}

// Verifica se um plano de lado cabe no estado atual sem colisões, disparos
// antecipados ou alcance cruzado de som.
bool planFits(const SidePlan& plan, const PlacementContext& context, int pathIdx, int simStep)
{
    QSet<quint64> planCells;
    auto cellsFree = [&](const QVector<LayerCell>& cells) {
        for (const LayerCell& cell : cells) {
            const quint64 key = packLayerCell(cell.cell, cell.layer);
            if (context.occupied.contains(key) || planCells.contains(key)) {
                return false;
            }
            if (cell.layer == 0 && context.rails.contains(packCell(cell.cell))) {
                return false;
            }
            planCells.insert(key);
        }
        return true;
    };

    for (const PlannedItem& laser : plan.lasers) {
        if (!cellsFree(plannedOccupiedCells(laser))) {
            return false;
        }
    }
    for (const PlannedItem& pin : plan.pinwheels) {
        if (!cellsFree(plannedOccupiedCells(pin))) {
            return false;
        }
    }
    for (const auto& pair : plan.soundItems) {
        if (!cellsFree(plannedOccupiedCells(pair.first))) {
            return false;
        }
    }

    // Feixes: elo 0 cobre o próprio trilho + a célula além; elos seguintes não
    // podem cruzar trilho algum.
    for (int i = 0; i < plan.lasers.size(); ++i) {
        const QVector<LayerCell> beam = laserBeamCells(toItem(plan.lasers[i]));
        for (int beamIdx = 0; beamIdx < beam.size(); ++beamIdx) {
            const quint64 key = packCell(beam[beamIdx].cell);
            if (!context.rails.contains(key)) {
                continue;
            }
            const int railIdx = context.railPathIndex.value(key, -1);
            if (i == 0 && beamIdx == 0 && railIdx == pathIdx) {
                continue; // o próprio trilho do step
            }
            if (i == 0 && beamIdx == 1 && railIdx == pathIdx + 1) {
                continue; // célula seguinte do caminho: laser permanece ativo
            }
            return false;
        }
    }

    // Alcance de som cruzado entre steps.
    QSet<quint64> newSoundCells;
    for (const auto& pair : plan.soundItems) {
        for (const LayerCell& cell : plannedOccupiedCells(pair.first)) {
            newSoundCells.insert(packLayerCell(cell.cell, cell.layer));
        }
    }
    QSet<quint64> newPinCells;
    for (const PlannedItem& pin : plan.pinwheels) {
        newPinCells.insert(packLayerCell(pin.cell, pin.layer));
    }

    for (const PlannedItem& planned : plan.lasers) {
        const Item laser = toItem(planned);
        for (const LayerCell& target : laserSoundTargetCells(laser)) {
            const quint64 key = packLayerCell(target.cell, target.layer);
            if (context.soundCellStep.contains(key)) {
                return false; // tocaria tapete de outro step/lado
            }
            const auto pinIt = context.pinwheelCellStep.constFind(key);
            if (pinIt != context.pinwheelCellStep.constEnd() && *pinIt != simStep) {
                return false; // acionaria cadeia de outro step
            }
        }
    }
    for (const auto& laserPair : context.placedLasers) {
        if (laserReachesCells(laserPair.first, newSoundCells)) {
            return false;
        }
        if (laserPair.second != simStep) {
            for (const LayerCell& target : laserSoundTargetCells(laserPair.first)) {
                if (newPinCells.contains(packLayerCell(target.cell, target.layer))) {
                    return false;
                }
            }
        }
    }
    return true;
}

void commitPlan(SidePlan& plan, PlacementContext& context, int simStep)
{
    auto commitItem = [&](const PlannedItem& planned) {
        const int itemIndex = context.layoutItems.size();
        context.layoutItems.push_back(toItem(planned));
        for (const LayerCell& cell : plannedOccupiedCells(planned)) {
            context.occupied.insert(packLayerCell(cell.cell, cell.layer), itemIndex);
        }
        return itemIndex;
    };

    for (const PlannedItem& laser : plan.lasers) {
        commitItem(laser);
        context.placedLasers.push_back({toItem(laser), simStep});
    }
    for (const PlannedItem& pin : plan.pinwheels) {
        commitItem(pin);
        context.pinwheelCellStep.insert(packLayerCell(pin.cell, pin.layer), simStep);
    }
    for (const auto& pair : plan.soundItems) {
        commitItem(pair.first);
        for (const LayerCell& cell : plannedOccupiedCells(pair.first)) {
            context.soundCellStep.insert(packLayerCell(cell.cell, cell.layer), simStep);
        }
        context.expectedStepByCell.insert(
            packLayerCell(pair.first.cell, pair.first.layer), simStep);
    }
}

QPoint travelDirection(const QVector<QPoint>& path, int idx)
{
    QPoint outgoing;
    if (idx < path.size() - 1) {
        outgoing = path[idx + 1] - path[idx];
    } else {
        outgoing = path[idx] - path[idx - 1];
    }
    if (idx == 0) {
        return outgoing;
    }
    const QPoint incoming = path[idx] - path[idx - 1];
    if (incoming != outgoing) {
        // canto: preferir o segmento horizontal
        if (std::abs(incoming.x()) >= std::abs(incoming.y())) {
            return incoming;
        }
        if (std::abs(outgoing.x()) >= std::abs(outgoing.y())) {
            return outgoing;
        }
        return incoming;
    }
    return outgoing;
}

struct GeometryPlan {
    QVector<QPoint> path;
    bool cyclic = false;
    int period = 0;       // steps musicais no ciclo (retângulo)
    int musicOffset = 0;  // musicStep(idx) = (idx + offset) % period
    int width = 0;
    int height = 0;
    int rows = 0;
    QString error;
    QVector<int> connectorCols;
    QSet<int> rowYs;
};

GeometryPlan buildLinearPath(const QuantizedSong& song)
{
    GeometryPlan plan;
    for (int x = -1; x <= song.lastStep; ++x) {
        plan.path.push_back(QPoint(x, 0));
    }
    plan.width = plan.path.size();
    plan.height = 1;
    plan.rows = 1;
    return plan;
}

GeometryPlan buildZigzagPath(const QuantizedSong& song, const ImportProfile& profile)
{
    GeometryPlan plan;
    const int nTiles = song.lastStep + 2;
    const int gap = std::max(3, profile.zigzagRowGap);
    const int conn = gap - 1;
    const double aspect = std::max(0.2, profile.zigzagAspect);

    double bestScore = -1.0;
    int bestW = 0;
    int bestRows = 0;
    for (int rows = 2; rows <= std::max(2, nTiles / 4); ++rows) {
        const int height = (rows - 1) * gap + 1;
        const int numer = nTiles - (rows - 1) * conn;
        if (numer <= 0) {
            continue;
        }
        const int wMin = (numer + rows - 1) / rows;
        const int wTarget = std::max(wMin, static_cast<int>(std::lround(aspect * height)));
        for (int w = std::max(wMin, wTarget - 15); w <= wTarget + 15; ++w) {
            const int total = rows * w + (rows - 1) * conn;
            if (total < nTiles) {
                continue;
            }
            const double score = std::abs(w - aspect * height) + 0.01 * (total - nTiles);
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                bestW = w;
                bestRows = rows;
            }
        }
    }
    if (bestScore < 0) {
        // música curta demais para serpentear: caminho linear
        return buildLinearPath(song);
    }

    for (int r = 0; r < bestRows; ++r) {
        const int y = r * gap;
        if (r % 2 == 0) {
            for (int x = 0; x < bestW; ++x) {
                plan.path.push_back(QPoint(x, y));
            }
        } else {
            for (int x = bestW - 1; x >= 0; --x) {
                plan.path.push_back(QPoint(x, y));
            }
        }
        if (r < bestRows - 1) {
            const int xEnd = plan.path.last().x();
            for (int dy = 1; dy < gap; ++dy) {
                plan.path.push_back(QPoint(xEnd, y + dy));
            }
        }
    }
    plan.path = plan.path.mid(0, nTiles);
    plan.width = bestW;
    plan.height = (bestRows - 1) * gap + 1;
    plan.rows = bestRows;
    plan.connectorCols = {0, bestW - 1};
    for (int r = 0; r < bestRows; ++r) {
        plan.rowYs.insert(r * gap);
    }
    return plan;
}

GeometryPlan buildRectanglePath(const QuantizedSong& song, const ImportProfile& profile, QVector<DroppedNote>* dropped)
{
    GeometryPlan plan;
    int period = song.lastStep + 1;
    if (period % 2 != 0) {
        ++period; // perímetro precisa ser par
    }
    if (period < 12) {
        plan.error = "Música curta demais para um circuito retangular (mínimo de 12 steps).";
        return plan;
    }

    const int halfPerimeter = period / 2 + 2; // W + H
    const int wMin = 4;
    const int wMax = halfPerimeter - 4;
    if (wMax < wMin) {
        plan.error = "Música curta demais para um circuito retangular.";
        return plan;
    }
    const double squareness = std::clamp(profile.rectSquareness, 0.0, 1.0);
    const int wSquare = halfPerimeter / 2;
    const int wTarget = std::clamp(
        static_cast<int>(std::lround(wSquare + (1.0 - squareness) * (wMax - wSquare))), wMin, wMax);

    auto cornerIndices = [&](int w) {
        const int h = halfPerimeter - w;
        return QVector<int>{0, w - 1, w + h - 2, 2 * w + h - 3};
    };
    auto cornerNoteCount = [&](int w, int offset) {
        int count = 0;
        for (int corner : cornerIndices(w)) {
            if (song.steps.contains((corner + offset) % period)) {
                ++count;
            }
        }
        return count;
    };

    int chosenW = -1;
    int chosenOffset = -1;
    for (int distance = 0; distance <= wMax - wMin && chosenW < 0; ++distance) {
        for (int w : {wTarget - distance, wTarget + distance}) {
            if (w < wMin || w > wMax) {
                continue;
            }
            for (int offset = 0; offset < period; ++offset) {
                if (cornerNoteCount(w, offset) == 0) {
                    chosenW = w;
                    chosenOffset = offset;
                    break;
                }
            }
            if (chosenW >= 0) {
                break;
            }
        }
    }
    if (chosenW < 0) {
        // fallback: minimiza notas nos cantos e as descarta
        int bestCount = INT_MAX;
        for (int offset = 0; offset < period; ++offset) {
            const int count = cornerNoteCount(wTarget, offset);
            if (count < bestCount) {
                bestCount = count;
                chosenOffset = offset;
            }
        }
        chosenW = wTarget;
        if (dropped) {
            for (int corner : cornerIndices(chosenW)) {
                const int step = (corner + chosenOffset) % period;
                for (const StepNote& note : song.steps.value(step)) {
                    dropped->push_back({step, note.midi, "nota no canto do retângulo"});
                }
            }
        }
    }

    const int w = chosenW;
    const int h = halfPerimeter - w;
    for (int x = 0; x < w; ++x) {
        plan.path.push_back(QPoint(x, 0));
    }
    for (int y = 1; y < h; ++y) {
        plan.path.push_back(QPoint(w - 1, y));
    }
    for (int x = w - 2; x >= 0; --x) {
        plan.path.push_back(QPoint(x, h - 1));
    }
    for (int y = h - 2; y >= 1; --y) {
        plan.path.push_back(QPoint(0, y));
    }

    plan.cyclic = true;
    plan.period = period;
    plan.musicOffset = chosenOffset;
    plan.width = w;
    plan.height = h;
    plan.rows = 2;
    return plan;
}

} // namespace

GenerationResult generateTrack(const QuantizedSong& song, const ImportProfile& profile)
{
    GenerationResult result;
    result.dropped = song.dropped;
    result.stats.totalNotes = song.totalNotes;

    if (song.steps.isEmpty()) {
        result.error = "Nenhuma nota selecionada para gerar a pista.";
        return result;
    }

    GeometryPlan geometry;
    switch (profile.geometry) {
    case TrackGeometry::Linear:
        geometry = buildLinearPath(song);
        break;
    case TrackGeometry::Zigzag:
        geometry = buildZigzagPath(song, profile);
        break;
    case TrackGeometry::Rectangular:
        geometry = buildRectanglePath(song, profile, &result.dropped);
        break;
    }
    if (!geometry.error.isEmpty()) {
        result.error = geometry.error;
        return result;
    }

    QSet<int> cornerDropSteps;
    if (geometry.cyclic) {
        for (const DroppedNote& note : result.dropped) {
            if (note.reason == "nota no canto do retângulo") {
                cornerDropSteps.insert(note.step);
            }
        }
    }

    const QVector<QPoint>& path = geometry.path;
    PlacementContext context;
    for (int i = 0; i < path.size(); ++i) {
        context.rails.insert(packCell(path[i]));
        context.railPathIndex.insert(packCell(path[i]), i);
        context.layoutItems.push_back(Item{0, ItemKind::Rail, path[i], 0, 0});
    }

    const int maxPinwheelDepth = std::max(0, profile.maxPinwheelDepth);

    auto stepKeyForPathIdx = [&](int pathIdx) {
        if (geometry.cyclic) {
            return (pathIdx + geometry.musicOffset) % geometry.period;
        }
        return pathIdx - 1;
    };
    auto stepHasNotes = [&](int stepKey) {
        if (geometry.cyclic) {
            stepKey = ((stepKey % geometry.period) + geometry.period) % geometry.period;
        }
        return song.steps.contains(stepKey);
    };

    for (int pathIdx = 1; pathIdx < path.size(); ++pathIdx) {
        const int stepKey = stepKeyForPathIdx(pathIdx);
        const int simStep = pathIdx - 1;
        if (!song.steps.contains(stepKey) || cornerDropSteps.contains(stepKey)) {
            continue;
        }

        const QPoint rail = path[pathIdx];
        const QPoint entry = rail - path[pathIdx - 1];
        const QPoint direction = travelDirection(path, pathIdx);
        const QVector<QPoint> normals = {
            QPoint(direction.y(), -direction.x()),
            QPoint(-direction.y(), direction.x()),
        };

        QVector<QPoint> candidates;
        switch (profile.geometry) {
        case TrackGeometry::Linear:
            candidates = {QPoint(0, 1), QPoint(0, -1)};
            break;
        case TrackGeometry::Rectangular: {
            // lado externo do perímetro
            QPoint outward;
            if (rail.y() == 0) {
                outward = QPoint(0, -1);
            } else if (rail.x() == geometry.width - 1) {
                outward = QPoint(1, 0);
            } else if (rail.y() == geometry.height - 1) {
                outward = QPoint(0, 1);
            } else {
                outward = QPoint(-1, 0);
            }
            candidates = {outward};
            break;
        }
        case TrackGeometry::Zigzag: {
            candidates = normals;
            for (const QPoint& extra : {QPoint(0, -1), QPoint(0, 1), QPoint(1, 0), QPoint(-1, 0)}) {
                if (!candidates.contains(extra)) {
                    candidates.push_back(extra);
                }
            }
            break;
        }
        }

        auto sideOk = [&](const QPoint& outward) {
            if (outward == entry) {
                return false; // head-on garantido
            }
            const QPoint beyond = rail - outward;
            const auto beyondIdx = context.railPathIndex.constFind(packCell(beyond));
            if (beyondIdx != context.railPathIndex.constEnd() && *beyondIdx != pathIdx + 1) {
                return false; // disparo antecipado ou re-disparo em outra passagem
            }
            return true;
        };

        QVector<QPoint> freeSides;
        for (const QPoint& candidate : candidates) {
            if (sideOk(candidate) && !freeSides.contains(candidate)) {
                freeSides.push_back(candidate);
            }
        }

        QVector<StepNote> notes = song.steps.value(stepKey);
        if (freeSides.isEmpty()) {
            for (const StepNote& note : notes) {
                result.dropped.push_back({stepKey, note.midi, "sem lado livre no trilho"});
            }
            continue;
        }

        if (profile.geometry == TrackGeometry::Zigzag) {
            const bool onConnector = !geometry.rowYs.contains(rail.y());
            auto rank = [&](const QPoint& outward) {
                int score = 0;
                if (normals.contains(outward)) {
                    score += 10;
                }
                if (onConnector || geometry.connectorCols.contains(rail.x())) {
                    if (rail.x() == geometry.width - 1 && outward == QPoint(1, 0)) {
                        score += 5;
                    }
                    if (rail.x() == 0 && outward == QPoint(-1, 0)) {
                        score += 5;
                    }
                }
                return -score;
            };
            std::stable_sort(freeSides.begin(), freeSides.end(), [&](const QPoint& a, const QPoint& b) {
                return rank(a) < rank(b);
            });
        }

        const bool anchorsOk = !stepHasNotes(stepKey - 1) && !stepHasNotes(stepKey + 1);

        QVector<QPair<QPoint, QVector<StepNote>>> sides;
        if (freeSides.size() == 1) {
            sides.push_back({freeSides[0], notes});
        } else {
            const int firstCount = (notes.size() + 1) / 2;
            sides.push_back({freeSides[0], notes.mid(0, firstCount)});
            sides.push_back({freeSides[1], notes.mid(firstCount)});
        }

        for (auto& side : sides) {
            QVector<StepNote>& sideNotes = side.second;
            while (!sideNotes.isEmpty()) {
                SidePlan plan = planSide(rail, side.first, sideNotes, anchorsOk, maxPinwheelDepth);
                if (plan.ok && planFits(plan, context, pathIdx, simStep)) {
                    for (const auto& unplacedNote : plan.unplaced) {
                        result.dropped.push_back({stepKey, unplacedNote.first.midi, unplacedNote.second});
                    }
                    commitPlan(plan, context, simStep);
                    result.stats.placedNotes += plan.soundItems.size();
                    break;
                }
                const StepNote removed = sideNotes.takeLast();
                result.dropped.push_back({stepKey, removed.midi,
                    plan.ok ? "conflito de espaço no lado" : plan.failReason});
            }
        }
    }

    // Handcar no início do caminho.
    const QPoint cartCell = path.first();
    const QPoint firstDelta = path[1] - path[0];
    context.layoutItems.push_back(Item{0, ItemKind::Handcar, cartCell, 0, 0});

    result.layout.msPerTile = profile.msPerTile;
    result.layout.initialDirection = directionForDelta(firstDelta);
    result.layout.mapNorth = Direction::North;
    result.layout.items = context.layoutItems;

    // ---- validação estrutural + simulação fiel ao app ----
    GridModel model;
    QString buildError;
    if (!buildGridModelFromLayout(result.layout, &model, &buildError)) {
        result.error = "Falha ao montar o layout: " + buildError;
        return result;
    }

    const QVector<int> headOn = model.headOnLaserIds(result.layout.initialDirection);
    if (!headOn.isEmpty()) {
        result.warnings.push_back(
            QString("%1 laser(es) seriam acionados de frente pelo carrinho.").arg(headOn.size()));
    }

    QHash<quint64, int> expected = context.expectedStepByCell;
    QHash<quint64, int> hits;
    const LayoutSimulation::Result sim = LayoutSimulation::run(model, result.layout.initialDirection);
    if (!sim.error.isEmpty()) {
        result.error = "Simulação falhou: " + sim.error;
        return result;
    }
    int mismatches = 0;
    QStringList mismatchDetails;
    QHash<quint64, QPoint> anchorByKey;
    for (const Item& item : model.items()) {
        if (isSoundItem(item.kind)) {
            anchorByKey.insert(packLayerCell(item.anchor, item.layer), item.anchor);
        }
    }
    auto describeMismatch = [&](const QString& what, const QPoint& anchor, int layer, int step) {
        if (mismatchDetails.size() < 8) {
            mismatchDetails << QString("%1 em (%2,%3) L%4 step %5")
                                   .arg(what)
                                   .arg(anchor.x())
                                   .arg(anchor.y())
                                   .arg(layer)
                                   .arg(step);
        }
    };
    for (const LayoutSimulation::Event& event : sim.events) {
        const Item* item = model.findItem(event.itemId);
        if (!item || !isSoundItem(item->kind)) {
            continue;
        }
        const quint64 key = packLayerCell(item->anchor, item->layer);
        const auto expectedIt = expected.constFind(key);
        if (expectedIt == expected.constEnd()) {
            ++mismatches;
            describeMismatch("disparo inesperado", item->anchor, item->layer, event.step);
        } else if (*expectedIt != event.step) {
            ++mismatches;
            describeMismatch(QString("step errado (esperado %1)").arg(*expectedIt),
                item->anchor, item->layer, event.step);
        } else if (hits.value(key, 0) > 0) {
            ++mismatches;
            describeMismatch("disparo duplicado", item->anchor, item->layer, event.step);
        }
        hits[key] = hits.value(key, 0) + 1;
    }
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        if (!hits.contains(it.key())) {
            ++mismatches;
            describeMismatch("tapete mudo", anchorByKey.value(it.key()), -1, it.value());
        }
    }
    result.stats.simulatedEvents = sim.events.size();
    if (mismatches > 0) {
        result.warnings.push_back(
            QString("Simulação divergiu em %1 disparo(s); revise a pista antes de usar. [%2]")
                .arg(mismatches)
                .arg(mismatchDetails.join("; ")));
    }
    if (geometry.cyclic && !sim.loopClosed) {
        result.warnings.push_back("O circuito não fechou na simulação.");
    }

    // ---- estatísticas ----
    result.stats.pathTiles = path.size();
    result.stats.width = geometry.width;
    result.stats.height = geometry.height;
    result.stats.rows = geometry.rows;
    result.stats.rails = path.size();
    for (const Item& item : result.layout.items) {
        if (item.kind == ItemKind::Laser) {
            ++result.stats.lasers;
        } else if (isMusicMat(item.kind)) {
            ++result.stats.mats;
        } else if (isPercussion(item.kind)) {
            ++result.stats.percussion;
        } else if (item.kind == ItemKind::Pinwheel) {
            ++result.stats.pinwheels;
        }
    }
    result.stats.durationSeconds = path.size() * profile.msPerTile / 1000.0;

    result.ok = true;
    return result;
}
