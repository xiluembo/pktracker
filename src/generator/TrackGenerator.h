#pragma once

#include "LayoutSerializer.h"
#include "generator/ImportProfile.h"
#include "generator/NoteMapping.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

struct GenerationStats {
    int pathTiles = 0;
    int width = 0;
    int height = 0;
    int rows = 0;
    int rails = 0;
    int lasers = 0;
    int mats = 0;
    int percussion = 0;
    int pinwheels = 0;
    int placedNotes = 0;
    int totalNotes = 0;
    int simulatedEvents = 0;
    double durationSeconds = 0.0;
};

struct GenerationResult {
    bool ok = false;
    QString error;
    TrackLayout layout;
    GenerationStats stats;
    QVector<DroppedNote> dropped;
    QStringList warnings;
};

// Gera a pista completa a partir das notas quantizadas: caminho da geometria
// escolhida, lasers/tapetes/percussão/pinwheels, handcar, validação pelo
// GridModel e simulação de disparos idêntica ao app.
GenerationResult generateTrack(const QuantizedSong& song, const ImportProfile& profile);
