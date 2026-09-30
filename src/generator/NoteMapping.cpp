#include "NoteMapping.h"

#include <QtCore/QHash>
#include <QtCore/QMap>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <tuple>

namespace {

// Réplica do MIDI_MAP dos geradores Python: 8 bases diatônicas x 4 rotações
// (ordem N, S, W, E), primeira combinação vence em caso de enarmonia.
QHash<int, QPair<ItemKind, int>> buildMidiMap()
{
    static const QVector<QPair<ItemKind, int>> bases = {
        {ItemKind::MusicLowDo, 48}, {ItemKind::MusicRe, 50}, {ItemKind::MusicMi, 52},
        {ItemKind::MusicFa, 53}, {ItemKind::MusicSol, 55}, {ItemKind::MusicLa, 57},
        {ItemKind::MusicTi, 59}, {ItemKind::MusicHighDo, 60},
    };
    static const QVector<QPair<int, int>> rotOffsets = {{0, 0}, {2, 12}, {3, 1}, {1, 13}};

    QHash<int, QPair<ItemKind, int>> map;
    for (const auto& rotOffset : rotOffsets) {
        for (const auto& base : bases) {
            const int midi = base.second + rotOffset.second;
            if (!map.contains(midi)) {
                map.insert(midi, {base.first, rotOffset.first});
            }
        }
    }
    return map;
}

const QHash<int, QPair<ItemKind, int>>& midiMap()
{
    static const QHash<int, QPair<ItemKind, int>> map = buildMidiMap();
    return map;
}

} // namespace

int foldMidiToPlayableRange(int midi)
{
    while (midi < 48) {
        midi += 12;
    }
    while (midi > 73) {
        midi -= 12;
    }
    return midi;
}

bool midiInPlayableRange(int midi)
{
    return midi >= 48 && midi <= 73;
}

namespace {

int pitchClass(int midi)
{
    const int pc = midi % 12;
    return pc < 0 ? pc + 12 : pc;
}

QVector<int> playableOctaves(int midi)
{
    QVector<int> candidates;
    const int pc = pitchClass(midi);
    for (int mapped = 48 + pc; mapped <= 73; mapped += 12) {
        candidates.push_back(mapped);
    }
    return candidates;
}

int pickPlayableOctave(int idealMidi, int targetMidi)
{
    const QVector<int> candidates = playableOctaves(idealMidi);
    int best = foldMidiToPlayableRange(idealMidi);
    auto score = [&](int mapped) {
        return std::tuple<int, int>{std::abs(mapped - targetMidi), std::abs(mapped - idealMidi)};
    };
    for (int mapped : candidates) {
        if (score(mapped) < score(best)) {
            best = mapped;
        }
    }
    return best;
}

int bestPhraseOctaveOffset(const QVector<int>& pitches, double registerCenter)
{
    int bestOffset = 0;
    auto bestScore = std::tuple<int, int, long long>{
        std::numeric_limits<int>::max(), 0, 0};
    const long long center2 = std::llround(registerCenter * 2.0);
    for (int octaves = -4; octaves <= 4; ++octaves) {
        const int offset = octaves * 12;
        int outOfRange = 0;
        long long sum = 0;
        for (int pitch : pitches) {
            const int shifted = pitch + offset;
            if (!midiInPlayableRange(shifted)) {
                ++outOfRange;
            }
            sum += shifted;
        }
        const long long centerScore = std::llabs(2 * sum - center2 * pitches.size());
        const auto score = std::tuple<int, int, long long>{
            outOfRange, std::abs(offset), centerScore};
        if (score < bestScore) {
            bestScore = score;
            bestOffset = offset;
        }
    }
    return bestOffset;
}

bool phraseFitsWithOffset(const QVector<int>& pitches, int offset)
{
    for (int pitch : pitches) {
        if (!midiInPlayableRange(pitch + offset)) {
            return false;
        }
    }
    return true;
}

constexpr double kOnsetToleranceMs = 15.0;

double noteStartMs(const QVector<double>& startMs, int index)
{
    return index < startMs.size() ? startMs[index] : 0.0;
}

// Mapeia uma janela contígua: offset comum + contorno só na voz superior;
// notas do mesmo onset compartilham o delta de oitava do topo.
void mapWindow(
    QVector<int>& mapped,
    const QVector<int>& pitches,
    const QVector<double>& startMs,
    const QVector<int>& order,
    int begin,
    int end,
    double registerCenter)
{
    QVector<int> slice;
    slice.reserve(end - begin);
    for (int i = begin; i < end; ++i) {
        slice.push_back(pitches[order[i]]);
    }
    const int offset = bestPhraseOctaveOffset(slice, registerCenter);

    int prevTopIdeal = 0;
    int prevTopMapped = 0;
    bool hasPrevTop = false;

    int i = begin;
    while (i < end) {
        const double onset = noteStartMs(startMs, order[i]);
        int j = i + 1;
        while (j < end
               && std::abs(noteStartMs(startMs, order[j]) - onset) <= kOnsetToleranceMs) {
            ++j;
        }

        // Dentro do onset: grave → agudo (já ordenado assim em order).
        const int topIndex = order[j - 1];
        const int topIdeal = pitches[topIndex] + offset;
        const int topTarget = hasPrevTop
            ? prevTopMapped + (topIdeal - prevTopIdeal)
            : topIdeal;
        const int topMapped = pickPlayableOctave(topIdeal, topTarget);
        const int delta = topMapped - topIdeal;

        for (int k = i; k < j; ++k) {
            const int index = order[k];
            const int ideal = pitches[index] + offset;
            mapped[index] = pickPlayableOctave(ideal, ideal + delta);
        }

        prevTopIdeal = topIdeal;
        prevTopMapped = topMapped;
        hasPrevTop = true;
        i = j;
    }
}

void mapPhrase(
    QVector<int>& mapped,
    const QVector<int>& pitches,
    const QVector<double>& startMs,
    const QVector<int>& order,
    int begin,
    int end,
    double windowMs,
    double registerCenter)
{
    if (begin >= end) {
        return;
    }

    QVector<int> slice;
    slice.reserve(end - begin);
    for (int i = begin; i < end; ++i) {
        slice.push_back(pitches[order[i]]);
    }
    const int offset = bestPhraseOctaveOffset(slice, registerCenter);
    if (phraseFitsWithOffset(slice, offset) || windowMs <= 0.0) {
        mapWindow(mapped, pitches, startMs, order, begin, end, registerCenter);
        return;
    }

    // Frase não cabe: janelas de ~4 tempos com oitava própria.
    int windowBegin = begin;
    double windowOrigin = noteStartMs(startMs, order[begin]);
    for (int i = begin + 1; i < end; ++i) {
        const double start = noteStartMs(startMs, order[i]);
        if (start - windowOrigin >= windowMs) {
            mapWindow(mapped, pitches, startMs, order, windowBegin, i, registerCenter);
            windowBegin = i;
            windowOrigin = start;
        }
    }
    mapWindow(mapped, pitches, startMs, order, windowBegin, end, registerCenter);
}

} // namespace

QVector<int> mapPitchesToPlayableRange(
    const QVector<int>& transposedMidi,
    const QVector<double>& startMs,
    const QVector<double>& durMs,
    double phraseGapMs,
    double registerCenter)
{
    const int n = transposedMidi.size();
    QVector<int> mapped(n);
    if (n == 0) {
        return mapped;
    }

    QVector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double startA = a < startMs.size() ? startMs[a] : 0.0;
        const double startB = b < startMs.size() ? startMs[b] : 0.0;
        if (startA != startB) {
            return startA < startB;
        }
        return transposedMidi[a] < transposedMidi[b];
    });

    auto silenceBefore = [&](int orderIndex) {
        if (orderIndex <= 0) {
            return 0.0;
        }
        const int prev = order[orderIndex - 1];
        const int cur = order[orderIndex];
        const double prevEnd = (prev < startMs.size() ? startMs[prev] : 0.0)
            + (prev < durMs.size() ? durMs[prev] : 0.0);
        const double curStart = cur < startMs.size() ? startMs[cur] : 0.0;
        return curStart - prevEnd;
    };

    const double gap = std::max(0.0, phraseGapMs);
    const double windowMs = 2.0 * gap; // gap = 2 tempos → janela = 4 tempos
    int phraseBegin = 0;
    for (int i = 1; i <= n; ++i) {
        if (i < n && silenceBefore(i) < gap) {
            continue;
        }
        mapPhrase(mapped, transposedMidi, startMs, order, phraseBegin, i, windowMs, registerCenter);
        phraseBegin = i;
    }
    return mapped;
}

std::optional<QPair<ItemKind, int>> matForMidi(int midi)
{
    const auto it = midiMap().constFind(midi);
    if (it == midiMap().constEnd()) {
        return std::nullopt;
    }
    return *it;
}

QuantizedSong quantizeSong(const MidiSong& song, const ImportProfile& profile)
{
    QuantizedSong result;

    QHash<int, TrackImportSettings> settingsByTrack;
    for (const TrackImportSettings& settings : profile.tracks) {
        settingsByTrack.insert(settings.trackIndex, settings);
    }

    // Grade musical em ticks (como reduce_tico): msPerTile só afeta o playback.
    const int ppq = std::max(1, song.ppq);
    const int ticksPerStep = std::max(1, ppq / std::max(1, profile.gridDivisor));

    auto msToTick = [&](double ms) -> long {
        if (song.tempoChanges.isEmpty()) {
            const double bpm = std::max(1.0, song.initialBpm);
            return static_cast<long>(std::llround(ms * bpm * ppq / 60000.0));
        }
        const MidiTempoChange* prev = &song.tempoChanges.first();
        for (int i = 1; i < song.tempoChanges.size(); ++i) {
            if (song.tempoChanges[i].ms > ms) {
                break;
            }
            prev = &song.tempoChanges[i];
        }
        const double bpm = std::max(1.0, prev->bpm);
        return prev->tick
            + static_cast<long>(std::llround((ms - prev->ms) * bpm * ppq / 60000.0));
    };

    // Trechos selecionados, concatenados em sequência.
    QVector<TimeRangeMs> ranges = profile.ranges;
    if (ranges.isEmpty()) {
        ranges.push_back({0.0, song.totalMs});
    }
    std::sort(ranges.begin(), ranges.end(), [](const TimeRangeMs& a, const TimeRangeMs& b) {
        return a.startMs < b.startMs;
    });
    QVector<long> rangeStartTicks;
    QVector<int> rangeStepOffsets;
    int accumulated = 0;
    for (const TimeRangeMs& range : ranges) {
        const long startTick = msToTick(range.startMs);
        const long endTick = std::max(startTick + 1, msToTick(range.endMs));
        rangeStartTicks.push_back(startTick);
        rangeStepOffsets.push_back(accumulated);
        accumulated += std::max(
            1, static_cast<int>(std::llround(static_cast<double>(endTick - startTick) / ticksPerStep)));
    }

    auto stepForNote = [&](const MidiNote& note) -> std::optional<int> {
        for (int i = 0; i < ranges.size(); ++i) {
            if (note.startMs >= ranges[i].startMs && note.startMs <= ranges[i].endMs) {
                const long rel = note.tick - rangeStartTicks[i];
                return rangeStepOffsets[i]
                    + static_cast<int>(std::llround(static_cast<double>(rel) / ticksPerStep));
            }
        }
        return std::nullopt;
    };

    // melhor prioridade por (step, midi) para dedupe melódico
    QMap<int, QMap<int, StepNote>> melodicBest;
    QMap<int, QMap<int, StepNote>> percussionBest; // chave interna: ItemKind

    struct PendingMelodic {
        MidiNote note;
        int step = 0;
        int priority = 0;
        int transposed = 0;
        int mappedMidi = 0;
        TrackRole role = TrackRole::Accompaniment;
        bool outerHigh = false;
        bool outerLow = false;
    };
    QVector<PendingMelodic> allMelodic;

    for (const MidiNote& note : song.notes) {
        const auto it = settingsByTrack.constFind(note.track);
        if (it == settingsByTrack.constEnd() || !it->enabled) {
            continue;
        }
        const auto step = stepForNote(note);
        if (!step.has_value()) {
            continue;
        }

        if (it->role == TrackRole::Percussion) {
            ItemKind kind;
            if ((note.midi == 35 || note.midi == 36) && profile.mapKick) {
                kind = ItemKind::BigDrum;
            } else if ((note.midi == 38 || note.midi == 40) && profile.mapSnare) {
                kind = ItemKind::BooBox;
            } else {
                result.dropped.push_back({*step, note.midi, "percussão sem mapeamento"});
                continue;
            }
            StepNote stepNote;
            stepNote.midi = note.midi;
            stepNote.kind = kind;
            stepNote.percussion = true;
            stepNote.priority = it->priority;
            stepNote.sourceTrack = note.track;
            auto& bucket = percussionBest[*step];
            const int key = static_cast<int>(kind);
            if (!bucket.contains(key) || stepNote.priority < bucket[key].priority) {
                bucket[key] = stepNote;
            }
            continue;
        }

        // Ornamentos / grace notes: curtos E fora da grade (staccato no
        // tempo permanece — ex. Tico com dur≈meio step).
        if (profile.dropOrnaments && note.durTicks > 0
            && note.durTicks < ticksPerStep / 2) {
            const long nearest = static_cast<long>(
                std::llround(static_cast<double>(note.tick) / ticksPerStep))
                * ticksPerStep;
            const bool offGrid = std::abs(note.tick - nearest) >= ticksPerStep / 4;
            if (offGrid) {
                result.dropped.push_back({*step, note.midi, "ornamento (grace note)"});
                continue;
            }
        }

        PendingMelodic pending;
        pending.note = note;
        pending.step = *step;
        pending.priority = it->priority;
        pending.role = it->role;
        pending.transposed = note.midi + profile.transpose;
        allMelodic.push_back(pending);
    }

    auto noteStrength = [](const PendingMelodic& n) -> long long {
        return static_cast<long long>(n.note.velocity) * 20LL
            + std::min(200L, n.note.durTicks)
            - n.priority;
    };
    auto highScore = [&](const PendingMelodic& n) -> long long {
        return static_cast<long long>(n.note.midi) * 100LL + noteStrength(n);
    };
    auto lowScore = [&](const PendingMelodic& n) -> long long {
        return -static_cast<long long>(n.note.midi) * 100LL + noteStrength(n) / 4;
    };

    // Dobras de oitava (±12/±24): em cada cadeia, mantém só o extremo
    // agudo e o grave (power chord / 4 hands); descarta o meio.
    if (profile.collapseOctaveDoubles && !allMelodic.isEmpty()) {
        QMap<int, QVector<int>> indexesByStep;
        for (int i = 0; i < allMelodic.size(); ++i) {
            indexesByStep[allMelodic[i].step].push_back(i);
        }
        QVector<char> keep(allMelodic.size(), 1);
        for (auto it = indexesByStep.constBegin(); it != indexesByStep.constEnd(); ++it) {
            const QVector<int>& idxs = it.value();
            if (idxs.size() < 2) {
                continue;
            }
            QVector<int> parent(idxs.size());
            for (int i = 0; i < idxs.size(); ++i) {
                parent[i] = i;
            }
            auto find = [&](int a) {
                while (parent[a] != a) {
                    parent[a] = parent[parent[a]];
                    a = parent[a];
                }
                return a;
            };
            auto unite = [&](int a, int b) {
                a = find(a);
                b = find(b);
                if (a != b) {
                    parent[b] = a;
                }
            };
            for (int i = 0; i < idxs.size(); ++i) {
                for (int j = i + 1; j < idxs.size(); ++j) {
                    const int d = std::abs(
                        allMelodic[idxs[i]].note.midi - allMelodic[idxs[j]].note.midi);
                    if (d == 12 || d == 24) {
                        unite(i, j);
                    }
                }
            }
            QHash<int, QVector<int>> components;
            for (int i = 0; i < idxs.size(); ++i) {
                components[find(i)].push_back(idxs[i]);
            }
            for (auto cit = components.constBegin(); cit != components.constEnd(); ++cit) {
                const QVector<int>& comp = cit.value();
                if (comp.size() < 2) {
                    continue;
                }
                int hi = comp.first();
                int lo = comp.first();
                for (int idx : comp) {
                    if (allMelodic[idx].note.midi > allMelodic[hi].note.midi) {
                        hi = idx;
                    }
                    if (allMelodic[idx].note.midi < allMelodic[lo].note.midi) {
                        lo = idx;
                    }
                }
                for (int idx : comp) {
                    if (idx != hi && idx != lo) {
                        keep[idx] = 0;
                        result.dropped.push_back(
                            {allMelodic[idx].step, allMelodic[idx].note.midi,
                             "dobra de oitava"});
                    }
                }
            }
        }
        QVector<PendingMelodic> reduced;
        reduced.reserve(allMelodic.size());
        for (int i = 0; i < allMelodic.size(); ++i) {
            if (keep[i]) {
                reduced.push_back(allMelodic[i]);
            }
        }
        allMelodic.swap(reduced);
    }

    // max+min por step (skyline). Com Melodia/Baixo: topo da melodia + grave do baixo.
    if (profile.outerVoicesOnly && !allMelodic.isEmpty()) {
        QMap<int, QVector<int>> indexesByStep;
        for (int i = 0; i < allMelodic.size(); ++i) {
            indexesByStep[allMelodic[i].step].push_back(i);
        }
        QVector<char> keep(allMelodic.size(), 0);
        for (auto it = indexesByStep.constBegin(); it != indexesByStep.constEnd(); ++it) {
            const QVector<int>& idxs = it.value();
            QVector<int> highPool;
            QVector<int> lowPool;
            highPool.reserve(idxs.size());
            lowPool.reserve(idxs.size());
            for (int idx : idxs) {
                const TrackRole role = allMelodic[idx].role;
                if (role == TrackRole::Melody) {
                    highPool.push_back(idx);
                } else if (role == TrackRole::Bass) {
                    lowPool.push_back(idx);
                }
            }
            // Sem melodia no step → candidatos agudos = não-baixo; sem baixo → não-melodia.
            if (highPool.isEmpty()) {
                for (int idx : idxs) {
                    if (allMelodic[idx].role != TrackRole::Bass) {
                        highPool.push_back(idx);
                    }
                }
            }
            if (lowPool.isEmpty()) {
                for (int idx : idxs) {
                    if (allMelodic[idx].role != TrackRole::Melody) {
                        lowPool.push_back(idx);
                    }
                }
            }
            if (highPool.isEmpty()) {
                highPool = idxs;
            }
            if (lowPool.isEmpty()) {
                lowPool = idxs;
            }

            int high = highPool.first();
            int low = lowPool.first();
            for (int idx : highPool) {
                if (highScore(allMelodic[idx]) > highScore(allMelodic[high])
                    || (highScore(allMelodic[idx]) == highScore(allMelodic[high])
                        && allMelodic[idx].priority < allMelodic[high].priority)) {
                    high = idx;
                }
            }
            for (int idx : lowPool) {
                if (lowScore(allMelodic[idx]) > lowScore(allMelodic[low])
                    || (lowScore(allMelodic[idx]) == lowScore(allMelodic[low])
                        && allMelodic[idx].priority < allMelodic[low].priority)) {
                    low = idx;
                }
            }
            keep[high] = 1;
            allMelodic[high].outerHigh = true;
            keep[low] = 1;
            allMelodic[low].outerLow = true;
            for (int idx : idxs) {
                if (!keep[idx]) {
                    result.dropped.push_back(
                        {allMelodic[idx].step, allMelodic[idx].note.midi,
                         "voz interna (só melodia+baixo)"});
                }
            }
        }
        QVector<PendingMelodic> reduced;
        reduced.reserve(allMelodic.size());
        for (int i = 0; i < allMelodic.size(); ++i) {
            if (keep[i]) {
                reduced.push_back(allMelodic[i]);
            }
        }
        allMelodic.swap(reduced);
    }

    // Folga de frase em tempo musical (~2 semínimas), independente do playback.
    const double phraseBpm = std::max(1.0, suggestGridBpm(song));
    const double phraseGapMs = 2.0 * (60000.0 / phraseBpm);

    auto foldAndStore = [&](QVector<PendingMelodic>& pending, double registerCenter, bool naiveFold) {
        if (pending.isEmpty()) {
            return;
        }
        if (profile.foldToRange) {
            if (naiveFold) {
                // Nota a nota, como o reduce_tico: um baixo muito grave não
                // arrasta o ataque duas oitavas para cima (uníssono com a melodia).
                for (PendingMelodic& note : pending) {
                    note.mappedMidi = foldMidiToPlayableRange(note.transposed);
                }
            } else {
                QVector<int> pitches;
                QVector<double> starts;
                QVector<double> durs;
                pitches.reserve(pending.size());
                starts.reserve(pending.size());
                durs.reserve(pending.size());
                for (const PendingMelodic& note : pending) {
                    pitches.push_back(note.transposed);
                    starts.push_back(note.note.startMs);
                    durs.push_back(note.note.durMs);
                }
                const QVector<int> mapped = mapPitchesToPlayableRange(
                    pitches, starts, durs, phraseGapMs, registerCenter);
                for (int i = 0; i < pending.size(); ++i) {
                    pending[i].mappedMidi = mapped[i];
                }
            }
        } else {
            for (PendingMelodic& note : pending) {
                if (!midiInPlayableRange(note.transposed)) {
                    result.dropped.push_back({note.step, note.note.midi, "fora do range C3..C#5"});
                    note.mappedMidi = -1;
                } else {
                    note.mappedMidi = note.transposed;
                }
            }
        }

        for (const PendingMelodic& note : pending) {
            if (note.mappedMidi < 0) {
                continue;
            }
            int midi = note.mappedMidi;
            auto& bucket = melodicBest[note.step];
            // Grave não pode ficar no mesmo pitch nem ACIMA da melodia já
            // dobrada — isso apaga a linha de cima (Tico-Tico em t=-11).
            if (naiveFold && !bucket.isEmpty()) {
                int top = std::numeric_limits<int>::min();
                for (auto it = bucket.constBegin(); it != bucket.constEnd(); ++it) {
                    top = std::max(top, it.key());
                }
                while (midi >= top && midi - 12 >= 48) {
                    midi -= 12;
                }
                if (midi >= top) {
                    result.dropped.push_back(
                        {note.step, note.note.midi, "grave acima da melodia após dobra"});
                    continue;
                }
            }
            const auto mat = matForMidi(midi);
            if (!mat.has_value()) {
                result.dropped.push_back({note.step, note.note.midi, "sem tapete para a nota"});
                continue;
            }
            StepNote stepNote;
            stepNote.midi = midi;
            stepNote.kind = mat->first;
            stepNote.rotationQuarters = mat->second;
            stepNote.priority = note.priority;
            stepNote.sourceTrack = note.note.track;
            if (!bucket.contains(midi) || stepNote.priority < bucket[midi].priority) {
                bucket[midi] = stepNote;
            }
        }
    };

    if (profile.outerVoicesOnly) {
        QVector<PendingMelodic> highVoice;
        QVector<PendingMelodic> lowVoice;
        for (const PendingMelodic& note : allMelodic) {
            if (note.outerHigh) {
                highVoice.push_back(note);
            }
            if (note.outerLow && !note.outerHigh) {
                lowVoice.push_back(note);
            }
        }
        // Como reduce_tico: as duas vozes dobram nota a nota. Fold por
        // frase no agudo puxava picos para o médio e apagava o C5.
        foldAndStore(highVoice, 72.0, true);
        foldAndStore(lowVoice, 52.0, true);
    } else {
        QMap<int, QVector<PendingMelodic>> byTrack;
        for (const PendingMelodic& note : allMelodic) {
            byTrack[note.note.track].push_back(note);
        }
        for (auto trackIt = byTrack.begin(); trackIt != byTrack.end(); ++trackIt) {
            foldAndStore(trackIt.value(), 60.5, false);
        }
    }

    // Junta e ordena por prioridade (depois agudo primeiro), aplicando o teto
    // de notas por step.
    int minStep = -1;
    for (auto it = melodicBest.constBegin(); it != melodicBest.constEnd(); ++it) {
        if (minStep < 0 || it.key() < minStep) {
            minStep = it.key();
        }
    }
    for (auto it = percussionBest.constBegin(); it != percussionBest.constEnd(); ++it) {
        if (minStep < 0 || it.key() < minStep) {
            minStep = it.key();
        }
    }
    if (minStep < 0) {
        return result;
    }

    QVector<int> allSteps;
    for (auto it = melodicBest.constBegin(); it != melodicBest.constEnd(); ++it) {
        allSteps.push_back(it.key());
    }
    for (auto it = percussionBest.constBegin(); it != percussionBest.constEnd(); ++it) {
        if (!allSteps.contains(it.key())) {
            allSteps.push_back(it.key());
        }
    }

    for (int step : allSteps) {
        QVector<StepNote> notes;
        for (const StepNote& note : melodicBest.value(step)) {
            notes.push_back(note);
        }
        for (const StepNote& note : percussionBest.value(step)) {
            notes.push_back(note);
        }
        std::sort(notes.begin(), notes.end(), [](const StepNote& a, const StepNote& b) {
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            return a.midi > b.midi;
        });

        if (profile.outerVoicesOnly) {
            QVector<StepNote> melodic;
            QVector<StepNote> percussion;
            for (const StepNote& note : notes) {
                if (note.percussion) {
                    percussion.push_back(note);
                } else {
                    melodic.push_back(note);
                }
            }
            if (melodic.size() > 2) {
                // Já ordenado agudo→grave: first = max, last = min.
                const StepNote top = melodic.first();
                const StepNote bottom = melodic.last();
                for (int i = 1; i < melodic.size() - 1; ++i) {
                    result.dropped.push_back(
                        {step - minStep, melodic[i].midi, "voz interna (só melodia+baixo)"});
                }
                melodic.clear();
                melodic.push_back(top);
                if (bottom.midi != top.midi) {
                    melodic.push_back(bottom);
                }
            }
            notes = melodic;
            notes.append(percussion);
            std::sort(notes.begin(), notes.end(), [](const StepNote& a, const StepNote& b) {
                if (a.priority != b.priority) {
                    return a.priority < b.priority;
                }
                return a.midi > b.midi;
            });
        }

        while (notes.size() > profile.maxNotesPerStep) {
            const StepNote removed = notes.takeLast();
            result.dropped.push_back({step - minStep, removed.midi, "acima do máximo de notas por step"});
        }
        if (notes.isEmpty()) {
            continue;
        }
        const int normalized = step - minStep;
        result.steps.insert(normalized, notes);
        result.lastStep = std::max(result.lastStep, normalized);
        result.totalNotes += notes.size();
    }

    // Pedal/trêmulo: mesma altura no GRAVE em vários steps → 1 ataque/tempo.
    // Nunca afina a voz de topo (melodia).
    if (profile.mergePedalRepeats && !result.steps.isEmpty()) {
        const int beatSteps = std::max(1, profile.gridDivisor);
        QHash<int, QVector<int>> stepsByMidi;
        for (auto it = result.steps.constBegin(); it != result.steps.constEnd(); ++it) {
            int top = std::numeric_limits<int>::min();
            for (const StepNote& note : it.value()) {
                if (!note.percussion) {
                    top = std::max(top, note.midi);
                }
            }
            for (const StepNote& note : it.value()) {
                if (!note.percussion && note.midi < top) {
                    stepsByMidi[note.midi].push_back(it.key());
                }
            }
        }
        for (auto it = stepsByMidi.begin(); it != stepsByMidi.end(); ++it) {
            QVector<int>& steps = it.value();
            std::sort(steps.begin(), steps.end());
            steps.erase(std::unique(steps.begin(), steps.end()), steps.end());
            if (steps.size() < beatSteps) {
                continue;
            }
            int runBegin = 0;
            for (int i = 1; i <= steps.size(); ++i) {
                const bool endRun = i == steps.size() || steps[i] != steps[i - 1] + 1;
                if (!endRun) {
                    continue;
                }
                const int runLen = i - runBegin;
                if (runLen >= beatSteps) {
                    for (int j = runBegin; j < i; ++j) {
                        const int offsetInRun = j - runBegin;
                        if (offsetInRun % beatSteps == 0) {
                            continue;
                        }
                        const int step = steps[j];
                        auto sit = result.steps.find(step);
                        if (sit == result.steps.end()) {
                            continue;
                        }
                        QVector<StepNote>& notesAt = sit.value();
                        for (int n = notesAt.size() - 1; n >= 0; --n) {
                            if (!notesAt[n].percussion && notesAt[n].midi == it.key()) {
                                result.dropped.push_back(
                                    {step, notesAt[n].midi, "pedal/trêmulo fundido"});
                                notesAt.removeAt(n);
                                --result.totalNotes;
                            }
                        }
                        if (notesAt.isEmpty()) {
                            result.steps.erase(sit);
                        }
                    }
                }
                runBegin = i;
            }
        }
        result.lastStep = 0;
        for (auto it = result.steps.constBegin(); it != result.steps.constEnd(); ++it) {
            result.lastStep = std::max(result.lastStep, it.key());
        }
    }

    return result;
}

QuantizationPreviewStats buildQuantizationPreviewStats(
    const QuantizedSong& song, int msPerTile, int maxNotesPerStep)
{
    QuantizationPreviewStats stats;
    stats.totalNotes = song.totalNotes;
    stats.droppedNotes = song.dropped.size();
    stats.stepsWithNotes = song.steps.size();
    stats.tileCount = song.steps.isEmpty() ? 0 : song.lastStep + 1;
    stats.durationSeconds = stats.tileCount * std::max(1, msPerTile) / 1000.0;
    stats.notesPerStep = QVector<int>(stats.tileCount, 0);

    long long topSum = 0;
    int topCount = 0;
    int polySteps = 0;
    for (auto it = song.steps.constBegin(); it != song.steps.constEnd(); ++it) {
        const int step = it.key();
        const int count = it.value().size();
        if (step >= 0 && step < stats.notesPerStep.size()) {
            stats.notesPerStep[step] = count;
        }
        stats.maxPolyphony = std::max(stats.maxPolyphony, count);
        if (count > maxNotesPerStep) {
            ++stats.stepsOverCap;
        }
        if (count >= 2) {
            ++polySteps;
        }
        int top = std::numeric_limits<int>::min();
        int bottom = std::numeric_limits<int>::max();
        bool hasMelodic = false;
        for (const StepNote& note : it.value()) {
            if (note.percussion) {
                continue;
            }
            hasMelodic = true;
            top = std::max(top, note.midi);
            bottom = std::min(bottom, note.midi);
        }
        if (hasMelodic) {
            topSum += top;
            ++topCount;
            if (count >= 2 && top - bottom >= 12) {
                ++stats.octaveDoubleSteps;
            }
        }
    }
    if (stats.stepsWithNotes > 0) {
        stats.polyStepPercent = 100.0 * polySteps / stats.stepsWithNotes;
    }
    if (topCount > 0) {
        stats.avgTopMidi = static_cast<double>(topSum) / topCount;
    }
    for (const DroppedNote& note : song.dropped) {
        ++stats.dropsByReason[note.reason];
        if (note.reason.startsWith(QLatin1String("ornamento"))) {
            ++stats.ornamentDrops;
        } else if (note.reason.startsWith(QLatin1String("pedal"))) {
            ++stats.pedalMergeDrops;
        }
    }
    return stats;
}
