#pragma once

#include "midi/MidiSong.h"

#include <QtCore/QVector>

enum class TrackGeometry {
    Linear,
    Rectangular,
    Zigzag,
};

struct TrackImportSettings {
    int trackIndex = 0;
    bool enabled = false;
    int priority = 0; // menor = mais importante
    TrackRole role = TrackRole::Accompaniment;
};

struct TimeRangeMs {
    double startMs = 0.0;
    double endMs = 0.0;
};

struct ImportProfile {
    QVector<TrackImportSettings> tracks;
    QVector<TimeRangeMs> ranges; // vazio = música inteira

    // Velocidade de playback (ms por trilho). A quantização usa ticks/ppq +
    // gridDivisor; mudar msPerTile não deve fundir nem espalhar notas.
    int msPerTile = 125;
    int gridDivisor = 4; // subdivisões por semínima: 2, 3, 4 ou 8
    int transpose = 0;
    bool foldToRange = true; // dobra oitavas para caber em C3..C#5 (48..73)
    // Se true, em cada step mantém só o MIDI mais agudo e o mais grave (antes
    // da dobra). O grave dobra rumo ao C3; o agudo, ao terço alto. Evita que
    // oitavas da mesma nota (ex. 4 hands) colapsem num uníssono.
    // A escolha do agudo/grave pondera velocity e duração (skyline).
    // Com papéis Melodia/Baixo, prefere topo da melodia e grave do baixo
    // (piano RH/LH) em vez do max/min global do step.
    bool outerVoicesOnly = false;
    // Colapsa dobras ±12/±24 no mesmo step (power chord / 4 hands).
    bool collapseOctaveDoubles = true;
    // Descarta grace notes / ornamentos (duração < meio step).
    bool dropOrnaments = true;
    // Funde pedal/trêmulo: mesma altura em steps consecutivos → 1 ataque/tempo.
    bool mergePedalRepeats = true;
    int maxNotesPerStep = 6;

    bool mapKick = true;  // GM 35/36 -> Big drum
    bool mapSnare = true; // GM 38/40 -> Boo-in-the-box

    TrackGeometry geometry = TrackGeometry::Linear;
    double rectSquareness = 1.0; // 1 = mais quadrado, 0 = mais achatado
    double zigzagAspect = 1.0;   // proporção alvo largura/altura
    int zigzagRowGap = 7;

    // Profundidade máxima de encadeamento de pinwheels por lado.
    // 0 proíbe pinwheels; a preferência é sempre os 3 níveis do laser antes.
    int maxPinwheelDepth = 1;
};
