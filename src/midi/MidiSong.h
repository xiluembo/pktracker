#pragma once

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QVector>

// Leitura e análise de arquivos MIDI para o wizard de importação.
// Tempos absolutos em milissegundos são calculados a partir do mapa de tempo
// completo do arquivo, então mudanças de andamento são preservadas.

enum class TrackRole {
    Melody,
    Accompaniment,
    Bass,
    Percussion,
};

QString trackRoleName(TrackRole role);

struct MidiNote {
    int track = 0;
    int channel = 0;
    int midi = 0;
    int velocity = 0;
    long tick = 0;
    long durTicks = 0;
    double startMs = 0.0;
    double durMs = 0.0;
};

struct MidiTrackInfo {
    int index = 0;
    QString name;
    QString instrumentName;
    QVector<int> channels;
    QVector<int> programs;
    int noteCount = 0;
    int minMidi = 0;
    int maxMidi = 0;
    double avgMidi = 0.0;
    double firstNoteMs = 0.0;
    double lastNoteMs = 0.0;
    double notesPerSecond = 0.0;
    int maxChordSize = 0;
    bool isPercussion = false;
    TrackRole suggestedRole = TrackRole::Accompaniment;
};

struct MidiMarker {
    double ms = 0.0;
    QString text;
};

struct MidiTempoChange {
    long tick = 0;
    double ms = 0.0;
    double bpm = 120.0;
};

class MidiSong {
public:
    bool load(const QString& path, QString* error);

    QString path;
    int ppq = 480;
    double initialBpm = 120.0;
    double totalMs = 0.0;
    // Fórmula de compasso do arquivo (padrão 4/4 se ausente).
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    QVector<MidiNote> notes; // ordenadas por startMs
    QVector<MidiTrackInfo> tracks;
    QVector<MidiMarker> markers;
    QVector<MidiTempoChange> tempoChanges;
};

// Ticks por compasso: ppq * numerator * 4 / denominator (4/4 → 4*ppq, 3/4 → 3*ppq).
long barTicksForSong(const MidiSong& song);

// Compassos para dedupe de forma: em 2/4 usa hipermedida 4/4 (frases de
// choro/marcha batem com o KEEP do reduce_tico); nos demais, o compasso real.
long sectionBarTicksForSong(const MidiSong& song);

// Deslocamento da grade de compassos (anacruse / humanização): escolhe um
// offset em beats que maximiza onsets perto dos tempos. 0 = começa no tick 0.
long suggestBarGridOffsetTicks(
    const MidiSong& song, const QVector<int>& enabledTracks = {});

// Atribui suggestedRole: percussão (canal 10), baixo (avg < 48 ou nome
// Left/Bass), melodia = maior tessitura (empate: Melody/Piano/Right/Lead).
void assignSuggestedRoles(QVector<MidiTrackInfo>& tracks);

// BPM usado na grade: se as mudanças de andamento depois da primeira
// concentram-se no final da peça (últimos 10%), usa o tempo final
// (MIDI que “corrige” o andamento só no coda). Senão, o andamento do
// segmento mais longo.
double suggestGridBpm(const MidiSong& song);

// Melhor transposição em [-12, 12]. Avalia a nota já dobrada para C3..C#5
// e privilegia melodia perto de C5 (72), o terço agudo, com menos dobras.
// Com roles: Melodia=4, Baixo=2, Acompanhamento=1 (percussão peso 0).
int suggestTranspose(
    const QVector<MidiNote>& notes,
    const QVector<int>& enabledTracks,
    const QHash<int, TrackRole>& rolesByTrack = {});

// ms/tile a partir do BPM e do divisor da grade (subdivisões por semínima).
int msPerTileFromBpm(double bpm, int gridDivisor);

// Sugere o divisor da grade rítmica (subdivisões por semínima) entre os
// candidatos do wizard: 2=colcheia, 3=tercina, 4=semicolcheia, 8=fusa.
// Escolhe o mais grosso que ainda alinha bem os onsets das faixas habilitadas.
int suggestGridDivisor(const MidiSong& song, const QVector<int>& enabledTracks);

// Índices de compassos a manter (usa a fórmula do arquivo). Omite reprises
// e rabos; ecos locais e pontes curtas permanecem. Fingerprint tolera até
// Fingerprint tolera até ~10% de células diferentes se a reprise estiver
// ≥8 compassos atrás (ornamentos / baixo variado).
QVector<int> suggestUniqueBars(
    const MidiSong& song,
    const QVector<int>& enabledTracks,
    int gridDivisor = 4);

// Intervalos em ms correspondentes a suggestUniqueBars, para concatenar.
QVector<QPair<double, double>> suggestUniqueBarRangesMs(
    const MidiSong& song,
    const QVector<int>& enabledTracks,
    int gridDivisor = 4);

// Heurística: muitos steps com >2 notas melódicas → vale reduzir a textura.
bool songLooksHomophonicDense(
    const MidiSong& song, const QVector<int>& melodicTracks, int gridDivisor = 4);
