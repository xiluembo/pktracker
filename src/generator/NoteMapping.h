#pragma once

#include "ItemTypes.h"
#include "generator/ImportProfile.h"

#include <QtCore/QMap>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <optional>

struct StepNote {
    int midi = 0;              // já transposto/dobrado (48..73) quando melódico
    ItemKind kind = ItemKind::MusicLowDo;
    int rotationQuarters = 0;  // rotação de tela para mapNorth = North
    bool percussion = false;
    int priority = 0;
    int sourceTrack = 0;
};

struct DroppedNote {
    int step = 0;
    int midi = 0;
    QString reason;
};

struct QuantizedSong {
    QMap<int, QVector<StepNote>> steps;
    int lastStep = 0;
    int totalNotes = 0;
    QVector<DroppedNote> dropped;
};

int foldMidiToPlayableRange(int midi);
bool midiInPlayableRange(int midi);

// Recoloca alturas já transpostas em C3..C#5 (48..73) preservando o contorno:
// um deslocamento de oitava comum por frase e, no que ainda sobra, a oitava
// tocável mais próxima do intervalo original. phraseGapMs é o silêncio mínimo
// que separa frases (e reinicia o registro).
QVector<int> mapPitchesToPlayableRange(
    const QVector<int>& transposedMidi,
    const QVector<double>& startMs,
    const QVector<double>& durMs,
    double phraseGapMs,
    double registerCenter = 60.5);

// (kind, rotationQuarters) do tapete que toca a nota, com mapNorth = North.
std::optional<QPair<ItemKind, int>> matForMidi(int midi);

QuantizedSong quantizeSong(const MidiSong& song, const ImportProfile& profile);

// Estatísticas da prévia rítmica (sem UI), usadas pelo wizard e pelos testes.
struct QuantizationPreviewStats {
    int stepsWithNotes = 0;
    int tileCount = 0;
    int totalNotes = 0;
    int droppedNotes = 0;
    int maxPolyphony = 0;
    int stepsOverCap = 0;
    double durationSeconds = 0.0;
    // Fidelidade / percepção da melodia.
    double polyStepPercent = 0.0;   // % dos steps com ≥2 notas
    double avgTopMidi = 0.0;        // média da voz de topo
    int octaveDoubleSteps = 0;      // steps com intervalo ≥12 entre topo e grave
    int ornamentDrops = 0;
    int pedalMergeDrops = 0;
    QMap<QString, int> dropsByReason;
    QVector<int> notesPerStep; // índice = step; 0 nos steps silenciosos
};

QuantizationPreviewStats buildQuantizationPreviewStats(
    const QuantizedSong& song, int msPerTile, int maxNotesPerStep);
