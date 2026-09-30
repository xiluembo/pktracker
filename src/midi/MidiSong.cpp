#include "MidiSong.h"

#include <MidiFile.h>

#include <QtCore/QHash>
#include <QtCore/QMap>
#include <QtCore/QPair>
#include <QtCore/QString>

#include <algorithm>
#include <cmath>
#include <limits>

QString trackRoleName(TrackRole role)
{
    switch (role) {
    case TrackRole::Melody:
        return "Melodia";
    case TrackRole::Accompaniment:
        return "Acompanhamento";
    case TrackRole::Bass:
        return "Baixo";
    case TrackRole::Percussion:
        return "Percussão";
    }
    return "Acompanhamento";
}

namespace {

bool nameSuggestsMelody(const MidiTrackInfo& info)
{
    const QString hay = (info.name + QLatin1Char(' ') + info.instrumentName).toLower();
    return hay.contains(QLatin1String("melody"))
        || hay.contains(QLatin1String("piano"))
        || hay.contains(QLatin1String("lead"))
        || hay.contains(QLatin1String("viola"))
        || hay.contains(QLatin1String("right"))
        || hay.contains(QLatin1String("mão direita"))
        || hay.contains(QLatin1String("mao direita"));
}

bool nameSuggestsBass(const MidiTrackInfo& info)
{
    const QString hay = (info.name + QLatin1Char(' ') + info.instrumentName).toLower();
    return hay.contains(QLatin1String("bass"))
        || hay.contains(QLatin1String("left"))
        || hay.contains(QLatin1String("mão esquerda"))
        || hay.contains(QLatin1String("mao esquerda"));
}

} // namespace

void assignSuggestedRoles(QVector<MidiTrackInfo>& tracks)
{
    int bestMelody = -1;
    for (MidiTrackInfo& info : tracks) {
        if (info.noteCount == 0) {
            continue;
        }
        if (info.isPercussion) {
            info.suggestedRole = TrackRole::Percussion;
            continue;
        }
        if (info.avgMidi < 48.0 || nameSuggestsBass(info)) {
            info.suggestedRole = TrackRole::Bass;
            continue;
        }
        info.suggestedRole = TrackRole::Accompaniment;
        // 4 hands / oitavas na mesma faixa: polifonia não desclassifica.
        if (bestMelody < 0) {
            bestMelody = info.index;
            continue;
        }
        const MidiTrackInfo& best = tracks[bestMelody];
        if (info.avgMidi > best.avgMidi) {
            bestMelody = info.index;
        } else if (info.avgMidi == best.avgMidi) {
            const bool infoName = nameSuggestsMelody(info);
            const bool bestName = nameSuggestsMelody(best);
            if (infoName && !bestName) {
                bestMelody = info.index;
            } else if (infoName == bestName && info.noteCount > best.noteCount) {
                bestMelody = info.index;
            }
        }
    }
    if (bestMelody >= 0) {
        tracks[bestMelody].suggestedRole = TrackRole::Melody;
    }
}

bool MidiSong::load(const QString& filePath, QString* error)
{
    smf::MidiFile file;
    if (!file.read(filePath.toStdString())) {
        if (error) {
            *error = "Não foi possível ler o arquivo MIDI.";
        }
        return false;
    }
    file.makeAbsoluteTicks();
    file.linkNotePairs();
    file.doTimeAnalysis();

    path = filePath;
    ppq = file.getTicksPerQuarterNote();
    notes.clear();
    tracks.clear();
    markers.clear();
    tempoChanges.clear();
    initialBpm = 120.0;
    totalMs = 0.0;
    timeSignatureNumerator = 4;
    timeSignatureDenominator = 4;

    bool firstTempoSeen = false;
    bool firstTimeSigSeen = false;
    for (int trackIndex = 0; trackIndex < file.getTrackCount(); ++trackIndex) {
        MidiTrackInfo info;
        info.index = trackIndex;
        long chordTick = -1;
        int chordSize = 0;
        double midiSum = 0.0;
        bool firstNote = true;

        for (int eventIndex = 0; eventIndex < file[trackIndex].getEventCount(); ++eventIndex) {
            smf::MidiEvent& event = file[trackIndex][eventIndex];
            const double eventMs = event.seconds * 1000.0;

            if (event.isTempo()) {
                if (!firstTempoSeen) {
                    initialBpm = event.getTempoBPM();
                    firstTempoSeen = true;
                }
                tempoChanges.push_back({event.tick, eventMs, event.getTempoBPM()});
                continue;
            }
            if (event.isTimeSignature() && !firstTimeSigSeen && event.size() >= 5) {
                // FF 58 04 nn dd ... — dd é log2 do denominador.
                timeSignatureNumerator = std::max(1, static_cast<int>(event[3]));
                timeSignatureDenominator = std::max(1, 1 << static_cast<int>(event[4]));
                firstTimeSigSeen = true;
                continue;
            }
            if (event.isTrackName() && info.name.isEmpty()) {
                info.name = QString::fromStdString(event.getMetaContent()).trimmed();
                continue;
            }
            if (event.isInstrumentName() && info.instrumentName.isEmpty()) {
                info.instrumentName = QString::fromStdString(event.getMetaContent()).trimmed();
                continue;
            }
            if (event.isMarkerText()) {
                markers.push_back({eventMs, QString::fromStdString(event.getMetaContent()).trimmed()});
                continue;
            }
            if (event.isPatchChange()) {
                if (!info.programs.contains(event.getP1())) {
                    info.programs.push_back(event.getP1());
                }
                continue;
            }
            if (!event.isNoteOn() || event.getVelocity() == 0) {
                continue;
            }

            MidiNote note;
            note.track = trackIndex;
            note.channel = event.getChannel();
            note.midi = event.getKeyNumber();
            note.velocity = event.getVelocity();
            note.tick = event.tick;
            note.durTicks = event.getTickDuration();
            note.startMs = eventMs;
            note.durMs = event.getDurationInSeconds() * 1000.0;
            notes.push_back(note);

            if (!info.channels.contains(note.channel)) {
                info.channels.push_back(note.channel);
            }
            ++info.noteCount;
            midiSum += note.midi;
            if (firstNote) {
                info.minMidi = info.maxMidi = note.midi;
                info.firstNoteMs = note.startMs;
                firstNote = false;
            } else {
                info.minMidi = std::min(info.minMidi, note.midi);
                info.maxMidi = std::max(info.maxMidi, note.midi);
            }
            info.lastNoteMs = std::max(info.lastNoteMs, note.startMs);
            totalMs = std::max(totalMs, note.startMs + note.durMs);

            if (note.tick == chordTick) {
                ++chordSize;
            } else {
                chordTick = note.tick;
                chordSize = 1;
            }
            info.maxChordSize = std::max(info.maxChordSize, chordSize);
        }

        if (info.noteCount > 0) {
            info.avgMidi = midiSum / info.noteCount;
            const double spanSeconds = std::max(0.001, (info.lastNoteMs - info.firstNoteMs) / 1000.0);
            info.notesPerSecond = info.noteCount / spanSeconds;
            info.isPercussion = info.channels.size() == 1 && info.channels.first() == 9;
        }
        tracks.push_back(info);
    }

    std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startMs < b.startMs;
    });
    std::stable_sort(markers.begin(), markers.end(), [](const MidiMarker& a, const MidiMarker& b) {
        return a.ms < b.ms;
    });

    // Sugestão de função por faixa (ver assignSuggestedRoles).
    assignSuggestedRoles(tracks);

    if (notes.isEmpty()) {
        if (error) {
            *error = "O arquivo MIDI não contém notas.";
        }
        return false;
    }
    return true;
}

long barTicksForSong(const MidiSong& song)
{
    const int ppq = std::max(1, song.ppq);
    const int num = std::max(1, song.timeSignatureNumerator);
    const int den = std::max(1, song.timeSignatureDenominator);
    return std::max(1L, static_cast<long>(ppq) * num * 4 / den);
}

long sectionBarTicksForSong(const MidiSong& song)
{
    const long measure = barTicksForSong(song);
    // 2/4: frases de reprise costumam ser de 4 tempos (A/B/C do Tico-Tico).
    if (song.timeSignatureNumerator == 2 && song.timeSignatureDenominator == 4) {
        return measure * 2;
    }
    return measure;
}

long suggestBarGridOffsetTicks(const MidiSong& song, const QVector<int>& enabledTracks)
{
    if (song.ppq <= 0 || song.notes.isEmpty()) {
        return 0;
    }
    const long barTicks = sectionBarTicksForSong(song);
    const long beatTicks = std::max(1L, static_cast<long>(song.ppq));
    const long tol = std::max(1L, beatTicks / 4);

    long firstTick = -1;
    for (const MidiNote& note : song.notes) {
        if (!enabledTracks.isEmpty() && !enabledTracks.contains(note.track)) {
            continue;
        }
        if (firstTick < 0 || note.tick < firstTick) {
            firstTick = note.tick;
        }
    }
    if (firstTick <= 0) {
        return 0;
    }

    // Anacruse / humanização: se o 1º ataque está mais perto de um tempo
    // ≠ 0 do que do tick 0, desloca a grade para esse tempo.
    const long nearBeat = static_cast<long>(
        std::llround(static_cast<double>(firstTick) / beatTicks)) * beatTicks;
    if (nearBeat > 0) {
        const long distNear = std::abs(firstTick - nearBeat);
        if (distNear <= tol && distNear < firstTick) {
            return nearBeat % barTicks;
        }
    }
    return 0;
}

namespace {

int foldToPlayable(int midi)
{
    while (midi < 48) {
        midi += 12;
    }
    while (midi > 73) {
        midi -= 12;
    }
    return midi;
}

} // namespace

int suggestTranspose(
    const QVector<MidiNote>& notes,
    const QVector<int>& enabledTracks,
    const QHash<int, TrackRole>& rolesByTrack)
{
    auto weightFor = [&](int track) -> int {
        if (rolesByTrack.isEmpty()) {
            return 1;
        }
        const auto it = rolesByTrack.constFind(track);
        if (it == rolesByTrack.constEnd()) {
            return 1;
        }
        switch (*it) {
        case TrackRole::Melody:
            return 4;
        case TrackRole::Bass:
            return 2;
        case TrackRole::Accompaniment:
            return 1;
        case TrackRole::Percussion:
            return 0;
        }
        return 1;
    };

    int melodyMax = std::numeric_limits<int>::min();
    bool hasMelody = false;
    for (const MidiNote& note : notes) {
        if (!enabledTracks.contains(note.track)) {
            continue;
        }
        const auto roleIt = rolesByTrack.constFind(note.track);
        if (roleIt != rolesByTrack.constEnd() && *roleIt == TrackRole::Melody) {
            hasMelody = true;
            melodyMax = std::max(melodyMax, note.midi);
        }
    }

    int bestTranspose = 0;
    long long bestScore = std::numeric_limits<long long>::min();
    for (int transpose = -12; transpose <= 12; ++transpose) {
        long long score = 0;
        for (const MidiNote& note : notes) {
            if (!enabledTracks.contains(note.track)) {
                continue;
            }
            const int weight = weightFor(note.track);
            if (weight <= 0) {
                continue;
            }
            const int moved = note.midi + transpose;
            const int folded = foldToPlayable(moved);
            const int octavesFolded = std::abs(moved - folded) / 12;
            score += weight * 10;
            score -= weight * octavesFolded * 6;

            const auto roleIt = rolesByTrack.constFind(note.track);
            const bool melody = roleIt != rolesByTrack.constEnd()
                && *roleIt == TrackRole::Melody;
            if (!melody && moved >= 48 && moved <= 73) {
                score += weight;
            }
        }
        // O pico da melodia deve pousar em C5 (72), não a média — senão o
        // corpo médio escolhe t=+1 e o ataque 88 vira F4.
        if (hasMelody) {
            const int moved = melodyMax + transpose;
            const int folded = foldToPlayable(moved);
            const int octavesFolded = std::abs(moved - folded) / 12;
            score += 200LL * (12 - std::abs(folded - 72));
            score -= 80LL * octavesFolded;
        }
        if (score > bestScore
            || (score == bestScore && std::abs(transpose) < std::abs(bestTranspose))) {
            bestScore = score;
            bestTranspose = transpose;
        }
    }
    return bestTranspose;
}

double suggestGridBpm(const MidiSong& song)
{
    if (song.tempoChanges.isEmpty()) {
        return std::max(1.0, song.initialBpm);
    }
    if (song.tempoChanges.size() == 1) {
        return std::max(1.0, song.tempoChanges.first().bpm);
    }

    const double totalMs = std::max(1.0, song.totalMs);
    // Mudanças depois da primeira só no coda (últimos 10%) → andamento final.
    bool onlyCodaChanges = true;
    for (int i = 1; i < song.tempoChanges.size(); ++i) {
        if (song.tempoChanges[i].ms <= totalMs * 0.90) {
            onlyCodaChanges = false;
            break;
        }
    }
    if (onlyCodaChanges) {
        return std::max(1.0, song.tempoChanges.last().bpm);
    }

    double bestDur = -1.0;
    double bestBpm = song.tempoChanges.first().bpm;
    for (int i = 0; i < song.tempoChanges.size(); ++i) {
        const double start = song.tempoChanges[i].ms;
        const double end = (i + 1 < song.tempoChanges.size())
            ? song.tempoChanges[i + 1].ms
            : totalMs;
        const double dur = end - start;
        if (dur > bestDur) {
            bestDur = dur;
            bestBpm = song.tempoChanges[i].bpm;
        }
    }
    return std::max(1.0, bestBpm);
}

int msPerTileFromBpm(double bpm, int gridDivisor)
{
    const double safeBpm = std::max(1.0, bpm);
    const int divisor = std::max(1, gridDivisor);
    return std::max(1, static_cast<int>(std::lround(60000.0 / safeBpm / divisor)));
}

int suggestGridDivisor(const MidiSong& song, const QVector<int>& enabledTracks)
{
    // Ordem: do mais grosso ao mais fino. Empate favorece o mais grosso.
    static const int candidates[] = {2, 3, 4, 8};

    QVector<long> onsets;
    onsets.reserve(song.notes.size());
    for (const MidiNote& note : song.notes) {
        if (!enabledTracks.isEmpty() && !enabledTracks.contains(note.track)) {
            continue;
        }
        onsets.push_back(note.tick);
    }
    if (onsets.isEmpty() || song.ppq <= 0) {
        return 4;
    }

    struct Score {
        int divisor = 4;
        double hitRate = 0.0;
        double avgErr = 1.0;
    };
    Score best;
    bool haveBest = false;

    for (int divisor : candidates) {
        const double step = static_cast<double>(song.ppq) / divisor;
        if (step < 1.0) {
            continue;
        }
        // Tolerância: 15% do passo da grade, no mínimo 1 tick.
        const double tolerance = std::max(1.0, step * 0.15);
        int hits = 0;
        double errSum = 0.0;
        for (long tick : onsets) {
            const double nearest = std::round(tick / step) * step;
            const double err = std::abs(static_cast<double>(tick) - nearest);
            if (err <= tolerance) {
                ++hits;
            }
            errSum += err / step;
        }
        Score score;
        score.divisor = divisor;
        score.hitRate = static_cast<double>(hits) / onsets.size();
        score.avgErr = errSum / onsets.size();

        if (!haveBest) {
            best = score;
            haveBest = true;
            continue;
        }
        // Aceita grade mais fina só se melhorar o alinhamento de forma clara
        // (≥3 pp de hit rate, ou mesma taxa com erro médio menor).
        // Exige também que a grade grossa não esteja "quase perfeita" (≥97%):
        // evita subir para fusas por ruído quando a semicolcheia já basta.
        const double hitGain = score.hitRate - best.hitRate;
        if (best.hitRate >= 0.97 && hitGain < 0.02) {
            continue;
        }
        if (hitGain > 0.03 || (std::abs(hitGain) <= 0.03 && score.avgErr + 0.02 < best.avgErr)) {
            best = score;
        }
    }
    return best.divisor;
}

namespace {

struct UniqueBarAnalysis {
    int barCount = 0;
    long barTicks = 0;
    long barOffset = 0;
    long maxTick = 0;
    QVector<int> alias;
    QVector<bool> isCopy;
    QVector<bool> keep;
};

UniqueBarAnalysis analyzeUniqueBars(
    const MidiSong& song,
    const QVector<int>& enabledTracks,
    int gridDivisor)
{
    UniqueBarAnalysis out;
    if (song.ppq <= 0 || song.notes.isEmpty()) {
        return out;
    }

    const int divisor = std::max(1, gridDivisor);
    const long stepTicks = std::max(1L, static_cast<long>(song.ppq / divisor));
    out.barTicks = sectionBarTicksForSong(song);
    out.barOffset = suggestBarGridOffsetTicks(song, enabledTracks);
    const long barOffset = out.barOffset;
    const int stepsPerBar = std::max(1, static_cast<int>(out.barTicks / stepTicks));

    for (const MidiNote& note : song.notes) {
        if (!enabledTracks.isEmpty() && !enabledTracks.contains(note.track)) {
            continue;
        }
        out.maxTick = std::max(out.maxTick, note.tick);
    }
    // Inclui o offset na contagem para não cortar o último compasso.
    const long span = std::max(0L, out.maxTick - barOffset);
    out.barCount = static_cast<int>(span / out.barTicks) + 1;
    if (out.barCount <= 0) {
        return out;
    }

    auto snappedTick = [&](long tick) -> long {
        const long shifted = tick - barOffset;
        return static_cast<long>(std::llround(static_cast<double>(shifted) / stepTicks))
            * stepTicks;
    };

    QVector<QVector<QPair<int, int>>> barFingerprints(out.barCount);
    for (const MidiNote& note : song.notes) {
        if (!enabledTracks.isEmpty() && !enabledTracks.contains(note.track)) {
            continue;
        }
        const long snapped = snappedTick(note.tick);
        if (snapped < 0) {
            continue; // anacruse antes da grade
        }
        const int bar = static_cast<int>(snapped / out.barTicks);
        if (bar < 0 || bar >= out.barCount) {
            continue;
        }
        const long offset = snapped - static_cast<long>(bar) * out.barTicks;
        const int stepInBar = static_cast<int>(offset / stepTicks);
        barFingerprints[bar].push_back({std::clamp(stepInBar, 0, stepsPerBar - 1), note.midi});
    }
    for (QVector<QPair<int, int>>& fp : barFingerprints) {
        std::sort(fp.begin(), fp.end());
        fp.erase(std::unique(fp.begin(), fp.end()), fp.end());
    }

    QHash<QString, int> firstSeen;
    out.alias.resize(out.barCount);
    out.isCopy = QVector<bool>(out.barCount, false);
    QVector<int> uniqueOrder;
    uniqueOrder.reserve(out.barCount);
    for (int i = 0; i < out.barCount; ++i) {
        out.alias[i] = i;
        QString key;
        for (const auto& cell : barFingerprints[i]) {
            key += QString::number(cell.first) + QLatin1Char(':')
                + QString::number(cell.second) + QLatin1Char(';');
        }
        if (key.isEmpty()) {
            continue;
        }
        const auto it = firstSeen.constFind(key);
        if (it == firstSeen.constEnd()) {
            firstSeen.insert(key, i);
            uniqueOrder.push_back(i);
        } else {
            out.alias[i] = *it;
            out.isCopy[i] = true;
        }
    }

    // Fingerprint tolerante: ≥90% das células iguais → trata como reprise
    // (ornamento / baixo levemente diferente).
    auto barSimilarity = [](const QVector<QPair<int, int>>& a,
                            const QVector<QPair<int, int>>& b) -> double {
        if (a.isEmpty() && b.isEmpty()) {
            return 1.0;
        }
        const int denom = std::max(a.size(), b.size());
        if (denom <= 0) {
            return 0.0;
        }
        int matches = 0;
        int ia = 0;
        int ib = 0;
        while (ia < a.size() && ib < b.size()) {
            if (a[ia] == b[ib]) {
                ++matches;
                ++ia;
                ++ib;
            } else if (a[ia] < b[ib]) {
                ++ia;
            } else {
                ++ib;
            }
        }
        return static_cast<double>(matches) / denom;
    };

    for (int i = 0; i < out.barCount; ++i) {
        if (out.isCopy[i] || barFingerprints[i].isEmpty()) {
            continue;
        }
        int bestAlias = -1;
        double bestSim = 0.0;
        for (int u : uniqueOrder) {
            if (u >= i) {
                break;
            }
            const double sim = barSimilarity(barFingerprints[i], barFingerprints[u]);
            if (sim > bestSim) {
                bestSim = sim;
                bestAlias = u;
            }
        }
        if (bestAlias >= 0 && bestSim >= 0.90
            && (i - bestAlias) >= 8) {
            out.alias[i] = bestAlias;
            out.isCopy[i] = true;
        }
    }

    auto sequentialRunLength = [&](int index) {
        int begin = index;
        while (begin > 0 && out.isCopy[begin - 1]
               && out.alias[begin] == out.alias[begin - 1] + 1) {
            --begin;
        }
        int end = index + 1;
        while (end < out.barCount && out.isCopy[end]
               && out.alias[end] == out.alias[end - 1] + 1) {
            ++end;
        }
        return end - begin;
    };

    out.keep = QVector<bool>(out.barCount, true);
    for (int i = 0; i < out.barCount; ++i) {
        if (!out.isCopy[i]) {
            continue;
        }
        const int distance = i - out.alias[i];
        const int run = sequentialRunLength(i);
        const bool localEcho = distance <= 5 && run <= 2;
        const bool lastOfRun = i + 1 >= out.barCount
            || !out.isCopy[i + 1]
            || out.alias[i + 1] != out.alias[i] + 1;
        const bool isolatedBridge = run <= 2 && lastOfRun
            && (i + 1 >= out.barCount || !out.isCopy[i + 1]);
        out.keep[i] = localEcho || isolatedBridge;
    }

    // Rabo de 1 compasso colado numa reprise já cortada (Tico 16 e 56).
    for (int i = 0; i < out.barCount; ++i) {
        if (!out.keep[i] || !out.isCopy[i] || sequentialRunLength(i) != 1) {
            continue;
        }
        if (i > 0 && !out.keep[i - 1] && out.isCopy[i - 1]) {
            out.keep[i] = false;
        }
    }

    // Pickup órfão: único isolado antes de uma reprise longa (Tico 49 = A'').
    for (int i = 0; i < out.barCount; ++i) {
        if (out.isCopy[i] || !out.keep[i]) {
            continue;
        }
        const bool prevCopy = i > 0 && out.isCopy[i - 1];
        const int nextRun = (i + 1 < out.barCount && out.isCopy[i + 1])
            ? sequentialRunLength(i + 1) : 0;
        if (prevCopy && nextRun >= 4) {
            out.keep[i] = false;
        }
    }
    return out;
}

} // namespace

QVector<int> suggestUniqueBars(
    const MidiSong& song,
    const QVector<int>& enabledTracks,
    int gridDivisor)
{
    const UniqueBarAnalysis analysis = analyzeUniqueBars(song, enabledTracks, gridDivisor);
    QVector<int> bars;
    for (int i = 0; i < analysis.barCount; ++i) {
        if (analysis.keep[i]) {
            bars.push_back(i);
        }
    }
    return bars;
}

QVector<QPair<double, double>> suggestUniqueBarRangesMs(
    const MidiSong& song,
    const QVector<int>& enabledTracks,
    int gridDivisor)
{
    QVector<QPair<double, double>> ranges;
    const UniqueBarAnalysis analysis = analyzeUniqueBars(song, enabledTracks, gridDivisor);
    if (analysis.barCount <= 0) {
        return ranges;
    }

    auto tickToMs = [&](long tick) -> double {
        // Aproxima pelo mapa de tempo do arquivo quando disponível.
        if (song.tempoChanges.isEmpty()) {
            return tick * (60000.0 / std::max(1.0, song.initialBpm) / song.ppq);
        }
        // Usa proporção linear com totalMs se o tick máximo for conhecido.
        if (analysis.maxTick > 0 && song.totalMs > 0.0) {
            return std::min(song.totalMs, tick * (song.totalMs / analysis.maxTick));
        }
        return tick * (60000.0 / std::max(1.0, song.initialBpm) / song.ppq);
    };

    int runStart = -1;
    for (int i = 0; i <= analysis.barCount; ++i) {
        const bool on = i < analysis.barCount && analysis.keep[i];
        if (on && runStart < 0) {
            runStart = i;
        } else if (!on && runStart >= 0) {
            const double startMs = tickToMs(
                analysis.barOffset + static_cast<long>(runStart) * analysis.barTicks);
            const double endMs = tickToMs(
                analysis.barOffset + static_cast<long>(i) * analysis.barTicks);
            if (endMs > startMs) {
                ranges.push_back({startMs, endMs});
            }
            runStart = -1;
        }
    }
    return ranges;
}

bool songLooksHomophonicDense(
    const MidiSong& song, const QVector<int>& melodicTracks, int gridDivisor)
{
    if (song.ppq <= 0 || melodicTracks.isEmpty()) {
        return false;
    }
    const int divisor = std::max(1, gridDivisor);
    const double step = static_cast<double>(song.ppq) / divisor;
    QHash<long, int> countByStep;
    int total = 0;
    for (const MidiNote& note : song.notes) {
        if (!melodicTracks.contains(note.track)) {
            continue;
        }
        const long q = static_cast<long>(std::llround(note.tick / step));
        ++countByStep[q];
        ++total;
    }
    if (countByStep.isEmpty() || total < 32) {
        return false;
    }
    int dense = 0;
    for (auto it = countByStep.constBegin(); it != countByStep.constEnd(); ++it) {
        if (it.value() > 2) {
            ++dense;
        }
    }
    return dense * 100 >= countByStep.size() * 15; // ≥15% dos steps com >2 notas
}
