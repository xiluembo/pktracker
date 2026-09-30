#pragma once

#include "GridModel.h"

#include <QtCore/QString>
#include <QtCore/QVector>

#include <functional>
#include <optional>

namespace LayoutSimulation {

// Próxima célula de trilho seguindo as mesmas regras do playback do planner
// (preferência: direção inicial, seguir reto, ordem N/E/S/W, mesma camada primeiro).
std::optional<LayerCell> nextRailCell(
    const GridModel& model,
    const LayerCell& current,
    const std::optional<LayerCell>& previous,
    Direction initialDirection);

// Dispara os lasers ativados pela célula do carrinho, com memória de lasers já
// ativos (borda de subida) e encadeamento por pinwheels. Cada item sonoro
// tocado é entregue ao callback.
void triggerLasersForCart(
    const GridModel& model,
    const LayerCell& cartCell,
    QVector<int>& activeLaserIds,
    const std::function<void(const Item&)>& onSoundItem);

struct Event {
    int step = 0; // -1 = disparo na célula inicial, antes do primeiro movimento
    int itemId = 0;
};

struct Result {
    QVector<Event> events;
    QVector<LayerCell> cartCells; // células visitadas, incluindo a inicial
    int stepsTaken = 0;
    bool endedAtDeadEnd = false;
    bool loopClosed = false;
    QString error;
};

// Simulação completa a partir do handcar. Para em beco sem saída, ao fechar um
// ciclo (mesma célula + mesma célula anterior) ou em maxSteps.
Result run(const GridModel& model, Direction initialDirection, int maxSteps = 200000);

} // namespace LayoutSimulation
