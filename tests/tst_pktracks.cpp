#include "GridModel.h"
#include "LayoutSerializer.h"
#include "LayoutSimulator.h"
#include "generator/NoteMapping.h"
#include "generator/TrackGenerator.h"
#include "midi/MidiSong.h"

#include <MidiFile.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtTest/QtTest>

#include <algorithm>

namespace {

StepNote melodicNote(int midi, int priority = 0)
{
    const auto mat = matForMidi(midi);
    StepNote note;
    note.midi = midi;
    note.kind = mat->first;
    note.rotationQuarters = mat->second;
    note.priority = priority;
    return note;
}

QuantizedSong songFromSteps(const QMap<int, QVector<int>>& midisByStep)
{
    QuantizedSong song;
    for (auto it = midisByStep.constBegin(); it != midisByStep.constEnd(); ++it) {
        QVector<StepNote> notes;
        for (int i = 0; i < it.value().size(); ++i) {
            notes.push_back(melodicNote(it.value()[i], i));
        }
        song.steps.insert(it.key(), notes);
        song.lastStep = std::max(song.lastStep, it.key());
        song.totalNotes += notes.size();
    }
    return song;
}

int countKind(const TrackLayout& layout, ItemKind kind)
{
    int count = 0;
    for (const Item& item : layout.items) {
        if (item.kind == kind) {
            ++count;
        }
    }
    return count;
}

} // namespace

class TestPktracks : public QObject {
    Q_OBJECT

private slots:
    void midiMapCoversPlayableRange();
    void foldKeepsRange();
    void foldPreservesPhraseContour();
    void foldResetsRegisterAfterRest();
    void foldSplitsLongPhraseIntoWindows();
    void foldMapsChordAsBlock();
    void assignSuggestedRolesPrefersHighTessitura();
    void serializerRoundTrip();
    void quantizeMergesAndDedupes();
    void quantizePreservesMelodyContour();
    void quantizeUsesGlobalTranspose();
    void quantizeRangesConcatenate();
    void quantizeMsPerTileDoesNotChangeSteps();
    void quantizationPreviewStats();
    void suggestGridDivisorPrefersCoarseWhenAligned();
    void suggestGridDivisorDetectsSixteenths();
    void suggestGridDivisorDetectsTriplets();
    void suggestTransposeWeightsMelody();
    void suggestTransposePrefersMelodyNearC5();
    void suggestGridBpmUsesCodaFinalTempo();
    void msPerTileFollowsBpmAndDivisor();
    void quantizeOuterVoicesOnlyKeepsExtremes();
    void quantizeOuterVoicesKeepsOctaveDoubling();
    void quantizeDoesNotFoldBassAboveMelody();
    void suggestUniqueBarRangesSkipsRepeats();
    void suggestUniqueBarRangesKeepsShortEchoes();
    void suggestUniqueBarRangesKeepsBridgeAfterReprise();
    void suggestUniqueBarsDropsTicoRepriseTails();
    void barTicksUsesTimeSignature();
    void suggestUniqueBarsToleratesSmallVariation();
    void quantizeDropsOrnaments();
    void quantizeSkylinePrefersLouderTop();
    void quantizeMergesPedalRepeats();
    void suggestBarGridOffsetAlignsHumanized();
    void quantizeCollapsesOctaveDoubles();
    void quantizeOuterVoicesPrefersMelodyAndBassRoles();
    void songLooksHomophonicDenseDetectsChords();
    void linearGenerationMatchesSimulation();
    void linearPolyphonyUsesPinwheelChain();
    void pinwheelDepthZeroDropsInsteadOfChaining();
    void rectangularGenerationLoops();
    void rectangularSquarenessChangesShape();
    void zigzagGenerationMatchesSimulation();
    void percussionGeneration();
    void midiFileEndToEnd();
    void ticoTicoImportMatchesSimplified();
    void regressionExistingLayouts();
    void gridModelIndexesMatchBruteForce();
    void gridModelIndexesSurviveMutations();
    void playbackHotPathKeepsPace();
};

void TestPktracks::midiMapCoversPlayableRange()
{
    for (int midi = 48; midi <= 73; ++midi) {
        const auto mat = matForMidi(midi);
        QVERIFY2(mat.has_value(), qPrintable(QString("sem tapete para %1").arg(midi)));
        // O tapete com essa rotação precisa tocar exatamente essa nota.
        QCOMPARE(playedMidiForItem(mat->first, mat->second, Direction::North), midi);
    }
    QVERIFY(!matForMidi(47).has_value());
    QVERIFY(!matForMidi(74).has_value());
}

void TestPktracks::foldKeepsRange()
{
    QCOMPARE(foldMidiToPlayableRange(47), 59);
    QCOMPARE(foldMidiToPlayableRange(74), 62);
    QCOMPARE(foldMidiToPlayableRange(24), 48);
    QCOMPARE(foldMidiToPlayableRange(60), 60);
}

void TestPktracks::foldPreservesPhraseContour()
{
    // Pico acima de C#5: a frase inteira desce uma oitava em vez de só o pico.
    const QVector<int> pitches = {65, 68, 72, 76, 72, 68};
    const QVector<double> starts = {0, 120, 240, 360, 480, 600};
    const QVector<double> durs = {100, 100, 100, 100, 100, 100};
    QCOMPARE(mapPitchesToPlayableRange(pitches, starts, durs, 1000.0),
             QVector<int>({53, 56, 60, 64, 60, 56}));

    // Frase larga demais para um único deslocamento: depois do teto, continua
    // na oitava dobrada em vez de voltar a pular.
    const QVector<int> wide = {48, 60, 72, 76, 72};
    const QVector<double> wideStarts = {0, 120, 240, 360, 480};
    const QVector<double> wideDurs = {100, 100, 100, 100, 100};
    QCOMPARE(mapPitchesToPlayableRange(wide, wideStarts, wideDurs, 1000.0),
             QVector<int>({48, 60, 72, 64, 60}));

    // Nota isolada fora do range continua na dobra ingênua.
    QCOMPARE(mapPitchesToPlayableRange({84}, {0.0}, {100.0}, 1000.0), QVector<int>({72}));
    QCOMPARE(mapPitchesToPlayableRange({30}, {0.0}, {100.0}, 1000.0), QVector<int>({54}));
}

void TestPktracks::foldResetsRegisterAfterRest()
{
    // Clímax dobrado, depois silêncio de frase: o registro volta ao natural.
    const QVector<int> pitches = {72, 76, 60};
    const QVector<double> starts = {0.0, 120.0, 3000.0};
    const QVector<double> durs = {100.0, 100.0, 100.0};
    QCOMPARE(mapPitchesToPlayableRange(pitches, starts, durs, 1000.0),
             QVector<int>({60, 64, 60}));
}

void TestPktracks::foldSplitsLongPhraseIntoWindows()
{
    // Frase contínua que não cabe num único offset: gap=500ms → janela=1000ms.
    // Janela 1 (aguda) desce −24; janela 2 recomeça no registro natural.
    const QVector<int> pitches = {72, 84, 96, 55, 57};
    const QVector<double> starts = {0.0, 250.0, 500.0, 1100.0, 1300.0};
    const QVector<double> durs = {100.0, 100.0, 100.0, 100.0, 100.0};
    const QVector<int> mapped = mapPitchesToPlayableRange(pitches, starts, durs, 500.0);
    QCOMPARE(mapped, QVector<int>({48, 60, 72, 55, 57}));
}

void TestPktracks::foldMapsChordAsBlock()
{
    // Duas notas no mesmo onset (±15 ms): compartilham o delta do topo.
    const QVector<int> pitches = {36, 48, 60};
    const QVector<double> starts = {0.0, 8.0, 300.0};
    const QVector<double> durs = {100.0, 100.0, 100.0};
    const QVector<int> mapped = mapPitchesToPlayableRange(pitches, starts, durs, 1000.0);
    QCOMPARE(mapped.size(), 3);
    // Acorde 36+48 sobe +12 em bloco → 48+60 (intervalo 12 preservado).
    QCOMPARE(mapped[0], 48);
    QCOMPARE(mapped[1], 60);
    QCOMPARE(mapped[1] - mapped[0], 12);
    QVERIFY(midiInPlayableRange(mapped[2]));
}

void TestPktracks::assignSuggestedRolesPrefersHighTessitura()
{
    QVector<MidiTrackInfo> tracks(3);
    tracks[0].index = 0;
    tracks[0].name = "Pad";
    tracks[0].noteCount = 200;
    tracks[0].avgMidi = 52.0;
    tracks[0].maxChordSize = 1;

    tracks[1].index = 1;
    tracks[1].name = "Lead";
    tracks[1].noteCount = 40;
    tracks[1].avgMidi = 72.0;
    tracks[1].maxChordSize = 1;

    tracks[2].index = 2;
    tracks[2].name = "Drums";
    tracks[2].noteCount = 100;
    tracks[2].avgMidi = 40.0;
    tracks[2].isPercussion = true;
    tracks[2].maxChordSize = 1;

    assignSuggestedRoles(tracks);
    QCOMPARE(tracks[0].suggestedRole, TrackRole::Accompaniment);
    QCOMPARE(tracks[1].suggestedRole, TrackRole::Melody);
    QCOMPARE(tracks[2].suggestedRole, TrackRole::Percussion);

    // Empate de avgMidi: nome Piano vence a faixa sem nome melódico.
    QVector<MidiTrackInfo> tied(2);
    tied[0].index = 0;
    tied[0].name = "Strings";
    tied[0].noteCount = 80;
    tied[0].avgMidi = 65.0;
    tied[0].maxChordSize = 1;
    tied[1].index = 1;
    tied[1].name = "Piano";
    tied[1].noteCount = 50;
    tied[1].avgMidi = 65.0;
    tied[1].maxChordSize = 1;
    assignSuggestedRoles(tied);
    QCOMPARE(tied[0].suggestedRole, TrackRole::Accompaniment);
    QCOMPARE(tied[1].suggestedRole, TrackRole::Melody);
}

void TestPktracks::serializerRoundTrip()
{
    TrackLayout layout;
    layout.msPerTile = 107;
    layout.initialDirection = Direction::East;
    layout.mapNorth = Direction::West;
    layout.items = {
        Item{0, ItemKind::Rail, QPoint(0, 0), 0, 0},
        Item{0, ItemKind::Rail, QPoint(1, 0), 0, 0},
        Item{0, ItemKind::Laser, QPoint(1, -1), 2, 0},
        Item{0, ItemKind::MusicSol, QPoint(1, -2), 1, 1},
        Item{0, ItemKind::Handcar, QPoint(0, 0), 0, 0},
    };

    TrackLayout parsed;
    QString error;
    QVERIFY2(layoutFromJson(layoutToJson(layout), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.msPerTile, layout.msPerTile);
    QCOMPARE(parsed.initialDirection, layout.initialDirection);
    QCOMPARE(parsed.mapNorth, layout.mapNorth);
    QCOMPARE(parsed.items.size(), layout.items.size());
    for (int i = 0; i < parsed.items.size(); ++i) {
        QCOMPARE(parsed.items[i].kind, layout.items[i].kind);
        QCOMPARE(parsed.items[i].anchor, layout.items[i].anchor);
        QCOMPARE(parsed.items[i].rotationQuarters, layout.items[i].rotationQuarters);
        QCOMPARE(parsed.items[i].layer, layout.items[i].layer);
    }

    GridModel model;
    QVERIFY2(buildGridModelFromLayout(parsed, &model, &error), qPrintable(error));
    QCOMPARE(model.items().size(), layout.items.size());
}

void TestPktracks::suggestGridDivisorPrefersCoarseWhenAligned()
{
    MidiSong song;
    song.ppq = 480;
    // Onsets só em colcheias (ppq/2): grade 1/8 basta.
    for (int i = 0; i < 16; ++i) {
        song.notes.push_back(MidiNote{0, 0, 60, 100, i * 240L, 100, 0.0, 100.0});
    }
    QCOMPARE(suggestGridDivisor(song, {0}), 2);
}

void TestPktracks::suggestGridDivisorDetectsSixteenths()
{
    MidiSong song;
    song.ppq = 480;
    // Onsets em semicolcheias (ppq/4): precisa de 1/16.
    for (int i = 0; i < 32; ++i) {
        song.notes.push_back(MidiNote{0, 0, 60, 100, i * 120L, 50, 0.0, 50.0});
    }
    QCOMPARE(suggestGridDivisor(song, {0}), 4);
}

void TestPktracks::suggestGridDivisorDetectsTriplets()
{
    MidiSong song;
    song.ppq = 480;
    // Onsets em tercinas de colcheia (ppq/3).
    for (int i = 0; i < 24; ++i) {
        song.notes.push_back(MidiNote{0, 0, 60, 100, i * 160L, 50, 0.0, 50.0});
    }
    QCOMPARE(suggestGridDivisor(song, {0}), 3);
}

void TestPktracks::suggestTransposeWeightsMelody()
{
    // Contagem bruta favorece o acompanhamento grave (+12); com pesos a
    // melodia aguda vence (−12) porque Melodia=4 × notas > Acomp.=1 × notas.
    QVector<MidiNote> notes;
    for (int i = 0; i < 10; ++i) {
        notes.push_back(MidiNote{0, 0, 36, 100, i * 10L, 5, 0.0, 5.0}); // acc
    }
    for (int i = 0; i < 5; ++i) {
        // 85: só −12 (→73) cabe; −11 (→74) fica fora — evita empate de score.
        notes.push_back(MidiNote{1, 0, 85, 100, i * 10L, 5, 0.0, 5.0});
    }
    const int unweighted = suggestTranspose(notes, {0, 1});
    QHash<int, TrackRole> roles;
    roles.insert(0, TrackRole::Accompaniment);
    roles.insert(1, TrackRole::Melody);
    const int weighted = suggestTranspose(notes, {0, 1}, roles);
    QCOMPARE(unweighted, 12);
    // Pico 85: prefere pousar em C5 (84→72) a enfiar 73 sem dobra.
    QVERIFY(weighted < 0);
    const int foldedPeak = [](int midi) {
        while (midi < 48) {
            midi += 12;
        }
        while (midi > 73) {
            midi -= 12;
        }
        return midi;
    }(85 + weighted);
    QCOMPARE(foldedPeak, 72);
}

void TestPktracks::suggestTransposePrefersMelodyNearC5()
{
    // 4 hands no ataque: 88/76/64/52. t=-4 → 84/72 (C5); t=-11 → 65 (F4).
    QVector<MidiNote> notes;
    for (int i = 0; i < 24; ++i) {
        notes.push_back(MidiNote{0, 0, 88, 100, i * 10L, 5, 0.0, 5.0});
        notes.push_back(MidiNote{0, 0, 76, 100, i * 10L, 5, 0.0, 5.0});
        notes.push_back(MidiNote{1, 0, 64, 100, i * 10L, 5, 0.0, 5.0});
        notes.push_back(MidiNote{2, 0, 52, 100, i * 10L, 5, 0.0, 5.0});
        notes.push_back(MidiNote{3, 0, 40, 100, i * 10L, 5, 0.0, 5.0});
    }
    QHash<int, TrackRole> roles;
    roles.insert(0, TrackRole::Melody);
    roles.insert(1, TrackRole::Accompaniment);
    roles.insert(2, TrackRole::Accompaniment);
    roles.insert(3, TrackRole::Bass);
    QCOMPARE(suggestTranspose(notes, {0, 1, 2, 3}, roles), -4);
}

void TestPktracks::suggestGridBpmUsesCodaFinalTempo()
{
    // Como o Tico-Tico: ~104 bpm quase toda a peça, 70 só no coda.
    MidiSong song;
    song.initialBpm = 104.0;
    song.totalMs = 100000.0;
    song.tempoChanges = {
        MidiTempoChange{0, 0.0, 104.0},
        MidiTempoChange{1000, 95000.0, 70.0},
    };
    QCOMPARE(suggestGridBpm(song), 70.0);

    // Mudança no meio → segmento mais longo.
    song.tempoChanges = {
        MidiTempoChange{0, 0.0, 120.0},
        MidiTempoChange{1000, 20000.0, 90.0},
        MidiTempoChange{2000, 80000.0, 60.0},
    };
    QCOMPARE(suggestGridBpm(song), 90.0);
}

void TestPktracks::msPerTileFollowsBpmAndDivisor()
{
    QCOMPARE(msPerTileFromBpm(70.0, 4), 214);
    QCOMPARE(msPerTileFromBpm(120.0, 4), 125);
    QCOMPARE(msPerTileFromBpm(120.0, 2), 250);
}

void TestPktracks::quantizeOuterVoicesOnlyKeepsExtremes()
{
    MidiSong song;
    song.totalMs = 500.0;
    song.notes = {
        MidiNote{0, 0, 48, 100, 0, 100, 0.0, 100.0},
        MidiNote{0, 0, 55, 100, 0, 100, 0.0, 100.0},
        MidiNote{0, 0, 60, 100, 0, 100, 0.0, 100.0},
        MidiNote{0, 0, 67, 100, 0, 100, 0.0, 100.0},
    };
    ImportProfile profile;
    profile.msPerTile = 125;
    profile.outerVoicesOnly = true;
    profile.maxNotesPerStep = 6;
    profile.tracks = {TrackImportSettings{0, true, 0, TrackRole::Melody}};

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.steps.value(0).size(), 2);
    QSet<int> midis;
    for (const StepNote& note : quantized.steps.value(0)) {
        midis.insert(note.midi);
    }
    QVERIFY(midis.contains(48));
    QVERIFY(midis.contains(67));
    QVERIFY(!midis.contains(55));
    QVERIFY(!midis.contains(60));
}

void TestPktracks::quantizeOuterVoicesKeepsOctaveDoubling()
{
    // Tico-Tico: 4 oitavas no ataque + baixo que depois desce a 28.
    // A dobra por frase do grave inteiro subiria +24 e viraria uníssono;
    // max+min + dobra nota a nota no grave → 72 e 48.
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 70.0;
    song.totalMs = 4000.0;
    song.notes = {
        MidiNote{0, 0, 88, 100, 0, 100, 0.0, 100.0},
        MidiNote{1, 0, 76, 100, 0, 100, 0.0, 100.0},
        MidiNote{2, 0, 64, 100, 0, 100, 0.0, 100.0},
        MidiNote{3, 0, 52, 100, 0, 100, 0.0, 100.0},
        MidiNote{0, 0, 86, 100, 120, 100, 214.0, 100.0},
        MidiNote{3, 0, 50, 100, 120, 100, 214.0, 100.0},
        MidiNote{0, 0, 76, 100, 2400, 100, 2000.0, 100.0},
        MidiNote{3, 0, 28, 100, 2400, 100, 2000.0, 100.0},
    };
    ImportProfile profile;
    profile.gridDivisor = 4;
    profile.msPerTile = 214;
    profile.transpose = -4;
    profile.foldToRange = true;
    profile.outerVoicesOnly = true;
    profile.maxNotesPerStep = 4;
    profile.tracks = {
        TrackImportSettings{0, true, 0, TrackRole::Melody},
        TrackImportSettings{1, true, 1, TrackRole::Accompaniment},
        TrackImportSettings{2, true, 2, TrackRole::Accompaniment},
        TrackImportSettings{3, true, 3, TrackRole::Bass},
    };

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.steps.value(0).size(), 2);
    QSet<int> midis;
    for (const StepNote& note : quantized.steps.value(0)) {
        midis.insert(note.midi);
    }
    QVERIFY(midis.contains(72));
    QVERIFY(midis.contains(48));
    QCOMPARE(quantized.steps.value(1).size(), 2);
}

void TestPktracks::quantizeDoesNotFoldBassAboveMelody()
{
    // Melodia no chão do range (50); baixo 45 dobra para 57 e ficaria acima.
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    song.totalMs = 1000.0;
    song.notes = {
        MidiNote{0, 0, 50, 100, 0, 100, 0.0, 100.0},
        MidiNote{1, 0, 45, 100, 0, 100, 0.0, 100.0},
    };
    ImportProfile profile;
    profile.foldToRange = true;
    profile.outerVoicesOnly = true;
    profile.transpose = 0;
    profile.tracks = {
        TrackImportSettings{0, true, 0, TrackRole::Melody},
        TrackImportSettings{1, true, 1, TrackRole::Bass},
    };

    const QuantizedSong quantized = quantizeSong(song, profile);
    QSet<int> midis;
    for (const StepNote& note : quantized.steps.value(0)) {
        midis.insert(note.midi);
    }
    QVERIFY(midis.contains(50));
    QVERIFY(!midis.contains(57));
}

void TestPktracks::suggestUniqueBarRangesSkipsRepeats()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    // 4 compassos de tema + 4 de reprise idêntica + 2 novos.
    const QVector<QVector<int>> theme = {
        {60, 61, 62, 63},
        {64, 65, 66, 67},
        {67, 66, 65, 64},
        {63, 62, 61, 60},
    };
    auto addBar = [&](int bar, const QVector<int>& pitches) {
        for (int i = 0; i < pitches.size(); ++i) {
            const long tick = bar * 480L * 4 + i * 480L;
            song.notes.push_back(MidiNote{0, 0, pitches[i], 100, tick, 100, 0.0, 100.0});
        }
    };
    for (int bar = 0; bar < 4; ++bar) {
        addBar(bar, theme[bar]);
    }
    for (int bar = 0; bar < 4; ++bar) {
        addBar(4 + bar, theme[bar]);
    }
    addBar(8, {72, 73, 74, 75});
    addBar(9, {76, 77, 79, 81});
    song.totalMs = 10 * 4 * (60000.0 / 120.0);

    const auto ranges = suggestUniqueBarRangesMs(song, {0}, 2);
    QVERIFY(ranges.size() >= 2);
    double kept = 0.0;
    for (const auto& r : ranges) {
        kept += r.second - r.first;
    }
    // Tema + material novo (~60%); a reprise de 4 compassos some.
    QVERIFY(kept < song.totalMs * 0.75);
    QVERIFY(kept > song.totalMs * 0.45);
}

void TestPktracks::suggestUniqueBarRangesKeepsShortEchoes()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    const QVector<QVector<int>> bars = {
        {60, 62, 64, 65},
        {67, 69, 71, 72},
        {64, 62, 60, 59},
        {57, 55, 53, 52},
        {60, 64, 67, 72},
        {71, 69, 67, 65},
        {64, 62, 60, 59}, // eco de 2
        {57, 55, 53, 52}, // eco de 3
    };
    for (int bar = 0; bar < bars.size(); ++bar) {
        for (int i = 0; i < bars[bar].size(); ++i) {
            const long tick = bar * 480L * 4 + i * 480L;
            song.notes.push_back(MidiNote{0, 0, bars[bar][i], 100, tick, 100, 0.0, 100.0});
        }
    }
    song.totalMs = 8 * 4 * (60000.0 / 120.0);

    const auto ranges = suggestUniqueBarRangesMs(song, {0}, 2);
    double kept = 0.0;
    for (const auto& r : ranges) {
        kept += r.second - r.first;
    }
    // Eco interno de 2 compassos não é reprise: a forma inteira permanece.
    QVERIFY(kept > song.totalMs * 0.90);
}

void TestPktracks::suggestUniqueBarRangesKeepsBridgeAfterReprise()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    const QVector<QVector<int>> theme = {
        {60, 61, 62, 63},
        {64, 65, 66, 67},
        {67, 66, 65, 64},
        {63, 62, 61, 60},
    };
    auto addBar = [&](int bar, const QVector<int>& pitches) {
        for (int i = 0; i < pitches.size(); ++i) {
            const long tick = bar * 480L * 4 + i * 480L;
            song.notes.push_back(MidiNote{0, 0, pitches[i], 100, tick, 100, 0.0, 100.0});
        }
    };
    for (int bar = 0; bar < 4; ++bar) {
        addBar(bar, theme[bar]);
    }
    for (int bar = 0; bar < 4; ++bar) {
        addBar(4 + bar, theme[bar]);
    }
    addBar(8, theme[2]); // ponte de 2 compassos (como Tico 47–48)
    addBar(9, theme[3]);
    addBar(10, {72, 74, 76, 79});
    song.totalMs = 11 * 4 * (60000.0 / 120.0);

    const auto ranges = suggestUniqueBarRangesMs(song, {0}, 2);
    double kept = 0.0;
    for (const auto& r : ranges) {
        kept += r.second - r.first;
    }
    // Reprise 4–7 some; ponte (9) e o material novo (10) ficam, com o tema.
    QVERIFY(kept > song.totalMs * 0.45);
    QVERIFY(kept < song.totalMs * 0.75);
}

void TestPktracks::suggestUniqueBarsDropsTicoRepriseTails()
{
    const QString midiPath = QStringLiteral(
        "D:/Downloads/Telegram Desktop/Tico_Tico_no_fub_for__Hands__1781297373595.mid");
    if (!QFile::exists(midiPath)) {
        QSKIP("MIDI do Tico-Tico não encontrado");
    }
    MidiSong song;
    QString error;
    QVERIFY2(song.load(midiPath, &error), qPrintable(error));
    QVector<int> enabled;
    for (const MidiTrackInfo& track : song.tracks) {
        if (track.noteCount > 0 && !track.isPercussion) {
            enabled.push_back(track.index);
        }
    }
    const QVector<int> bars = suggestUniqueBars(song, enabled, 4);
    // Forma A+B+C+final; sem rabos 16/49/56. O fuzzy fingerprint pode
    // absorver a ponte 48 quando ela é ~igual a material anterior.
    QVERIFY(!bars.contains(16));
    QVERIFY(!bars.contains(49));
    QVERIFY(!bars.contains(56));
    QVERIFY(bars.contains(0));
    QVERIFY(bars.contains(17));
    QVERIFY(bars.contains(33));
    QVERIFY(bars.contains(57));
    QVERIFY(bars.size() >= 27 && bars.size() <= 32);
}

void TestPktracks::barTicksUsesTimeSignature()
{
    MidiSong song;
    song.ppq = 480;
    song.timeSignatureNumerator = 4;
    song.timeSignatureDenominator = 4;
    QCOMPARE(barTicksForSong(song), 1920L);

    song.timeSignatureNumerator = 3;
    song.timeSignatureDenominator = 4;
    QCOMPARE(barTicksForSong(song), 1440L);

    song.timeSignatureNumerator = 6;
    song.timeSignatureDenominator = 8;
    QCOMPARE(barTicksForSong(song), 1440L);

    song.timeSignatureNumerator = 2;
    song.timeSignatureDenominator = 4;
    QCOMPARE(barTicksForSong(song), 960L);
    QCOMPARE(sectionBarTicksForSong(song), 1920L); // hipermedida para reprises
}

void TestPktracks::suggestUniqueBarsToleratesSmallVariation()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    song.timeSignatureNumerator = 4;
    song.timeSignatureDenominator = 4;
    auto addBarContent = [&](int bar, int contentId, int mutateAt) {
        for (int i = 0; i < 16; ++i) {
            const long tick = bar * 1920L + i * 120L;
            int midi = 48 + (contentId % 12) + (i % 5);
            if (i == mutateAt) {
                ++midi;
            }
            song.notes.push_back(MidiNote{0, 0, midi, 100, tick, 50, 0.0, 50.0});
        }
    };
    // Tema 0–3 e material 4–7 (conteúdos distintos).
    for (int bar = 0; bar < 8; ++bar) {
        addBarContent(bar, bar, -1);
    }
    // Reprise 8–11 ≈ tema 0–3 (distância 8); bar 9 tem 1 célula diferente.
    for (int bar = 0; bar < 4; ++bar) {
        addBarContent(8 + bar, bar, bar == 1 ? 4 : -1);
    }
    song.totalMs = 12 * 4 * (60000.0 / 120.0);

    const QVector<int> bars = suggestUniqueBars(song, {0}, 4);
    QVERIFY(!bars.contains(8));
    QVERIFY(!bars.contains(9));
    QVERIFY(!bars.contains(10));
    QVERIFY(!bars.contains(11));
    QCOMPARE(bars, (QVector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
}

void TestPktracks::quantizeDropsOrnaments()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    song.totalMs = 1000.0;
    // Nota normal no grid + grace fora do grid (tick 30, dur 20).
    song.notes = {
        MidiNote{0, 0, 72, 100, 0, 120, 0.0, 100.0},
        MidiNote{0, 0, 74, 40, 30, 20, 62.0, 10.0},
    };
    ImportProfile profile;
    profile.gridDivisor = 4;
    profile.msPerTile = 125;
    profile.dropOrnaments = true;
    profile.mergePedalRepeats = false;
    profile.tracks = {TrackImportSettings{0, true, 0, TrackRole::Melody}};

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.totalNotes, 1);
    QCOMPARE(quantized.steps.value(0).first().midi, 72);
    QVERIFY(quantized.dropped.size() >= 1);
}

void TestPktracks::quantizeSkylinePrefersLouderTop()
{
    // Acompanhamento fraco acima da melodia forte: skyline escolhe a melodia.
    MidiSong song;
    song.ppq = 480;
    song.totalMs = 500.0;
    song.notes = {
        MidiNote{0, 0, 67, 110, 0, 200, 0.0, 200.0}, // melodia
        MidiNote{1, 0, 72, 30, 0, 40, 0.0, 40.0},    // arpejo fraco acima
        MidiNote{2, 0, 48, 90, 0, 200, 0.0, 200.0},  // baixo
    };
    ImportProfile profile;
    profile.gridDivisor = 4;
    profile.outerVoicesOnly = true;
    profile.dropOrnaments = false; // duração do arpejo pode ser < meio step
    profile.mergePedalRepeats = false;
    profile.tracks = {
        TrackImportSettings{0, true, 0, TrackRole::Melody},
        TrackImportSettings{1, true, 1, TrackRole::Accompaniment},
        TrackImportSettings{2, true, 2, TrackRole::Bass},
    };

    const QuantizedSong quantized = quantizeSong(song, profile);
    QSet<int> midis;
    for (const StepNote& note : quantized.steps.value(0)) {
        midis.insert(note.midi);
    }
    QVERIFY(midis.contains(67));
    QVERIFY(midis.contains(48));
    QVERIFY(!midis.contains(72));
}

void TestPktracks::quantizeMergesPedalRepeats()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    song.totalMs = 2000.0;
    // Melodia muda + baixo pedal na mesma altura em 8 steps.
    for (int i = 0; i < 8; ++i) {
        song.notes.push_back(MidiNote{0, 0, 72 - (i % 3), 100, i * 120L, 100, i * 125.0, 100.0});
        song.notes.push_back(MidiNote{1, 0, 48, 100, i * 120L, 100, i * 125.0, 100.0});
    }
    ImportProfile profile;
    profile.gridDivisor = 4;
    profile.msPerTile = 125;
    profile.outerVoicesOnly = true;
    profile.mergePedalRepeats = true;
    profile.dropOrnaments = false;
    profile.tracks = {
        TrackImportSettings{0, true, 0, TrackRole::Melody},
        TrackImportSettings{1, true, 1, TrackRole::Bass},
    };

    const QuantizedSong quantized = quantizeSong(song, profile);
    int bassHits = 0;
    for (auto it = quantized.steps.constBegin(); it != quantized.steps.constEnd(); ++it) {
        for (const StepNote& note : it.value()) {
            if (note.midi == 48) {
                ++bassHits;
            }
        }
    }
    // Pedal 48 só a cada tempo (2 hits em 8 steps), melodia intacta.
    QCOMPARE(bassHits, 2);
    QVERIFY(quantized.steps.size() >= 6);
}

void TestPktracks::suggestBarGridOffsetAlignsHumanized()
{
    MidiSong song;
    song.ppq = 480;
    song.timeSignatureNumerator = 4;
    song.timeSignatureDenominator = 4;
    // Ataques humanizados perto do tempo 1 (tick 480), não do 0.
    for (int bar = 0; bar < 4; ++bar) {
        const long down = bar * 1920L + 480L;
        song.notes.push_back(MidiNote{0, 0, 60, 100, down - 12, 100, 0.0, 100.0});
        song.notes.push_back(MidiNote{0, 0, 64, 100, down + 8, 100, 0.0, 100.0});
        song.notes.push_back(MidiNote{0, 0, 48, 90, down - 5, 100, 0.0, 100.0});
    }
    const long offset = suggestBarGridOffsetTicks(song, {0});
    QCOMPARE(offset, 480L);
}

void TestPktracks::quantizeCollapsesOctaveDoubles()
{
    MidiSong song;
    song.ppq = 480;
    song.totalMs = 500.0;
    song.notes = {
        MidiNote{0, 0, 72, 110, 0, 200, 0.0, 200.0},
        MidiNote{0, 0, 60, 90, 0, 200, 0.0, 200.0},  // +12 abaixo
        MidiNote{1, 0, 48, 100, 0, 200, 0.0, 200.0},
    };
    ImportProfile profile;
    profile.outerVoicesOnly = true;
    profile.collapseOctaveDoubles = true;
    profile.dropOrnaments = false;
    profile.mergePedalRepeats = false;
    profile.tracks = {
        TrackImportSettings{0, true, 0, TrackRole::Melody},
        TrackImportSettings{1, true, 1, TrackRole::Bass},
    };
    const QuantizedSong quantized = quantizeSong(song, profile);
    QSet<int> midis;
    for (const StepNote& note : quantized.steps.value(0)) {
        midis.insert(note.midi);
    }
    QVERIFY(midis.contains(72));
    QVERIFY(midis.contains(48));
    QVERIFY(!midis.contains(60));
    QVERIFY(quantized.dropped.size() >= 1);
}

void TestPktracks::quantizeOuterVoicesPrefersMelodyAndBassRoles()
{
    // Acompanhamento acima da melodia: com papéis, o topo vem da Melodia.
    MidiSong song;
    song.ppq = 480;
    song.totalMs = 500.0;
    song.notes = {
        MidiNote{0, 0, 67, 100, 0, 200, 0.0, 200.0}, // melodia
        MidiNote{1, 0, 79, 100, 0, 200, 0.0, 200.0}, // acomp. acima
        MidiNote{2, 0, 48, 100, 0, 200, 0.0, 200.0}, // baixo
    };
    ImportProfile profile;
    profile.outerVoicesOnly = true;
    profile.collapseOctaveDoubles = false;
    profile.dropOrnaments = false;
    profile.mergePedalRepeats = false;
    profile.tracks = {
        TrackImportSettings{0, true, 0, TrackRole::Melody},
        TrackImportSettings{1, true, 1, TrackRole::Accompaniment},
        TrackImportSettings{2, true, 2, TrackRole::Bass},
    };
    const QuantizedSong quantized = quantizeSong(song, profile);
    QSet<int> midis;
    for (const StepNote& note : quantized.steps.value(0)) {
        midis.insert(note.midi);
    }
    QVERIFY(midis.contains(67));
    QVERIFY(midis.contains(48));
    QVERIFY(!midis.contains(79));
}

void TestPktracks::songLooksHomophonicDenseDetectsChords()
{
    MidiSong sparse;
    sparse.ppq = 480;
    for (int i = 0; i < 40; ++i) {
        sparse.notes.push_back(MidiNote{0, 0, 60, 100, i * 120L, 50, 0.0, 50.0});
    }
    QVERIFY(!songLooksHomophonicDense(sparse, {0}, 4));

    MidiSong dense;
    dense.ppq = 480;
    for (int i = 0; i < 40; ++i) {
        for (int m = 0; m < 4; ++m) {
            dense.notes.push_back(MidiNote{0, 0, 48 + m * 4, 100, i * 120L, 50, 0.0, 50.0});
        }
    }
    QVERIFY(songLooksHomophonicDense(dense, {0}, 4));
}

void TestPktracks::quantizeMergesAndDedupes()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    song.totalMs = 5000.0;
    // Duas faixas com a mesma nota no mesmo instante + notas isoladas fora do range.
    // Grade: ticksPerStep = 480/4 = 120 → steps 0, 20, 32.
    song.notes = {
        MidiNote{0, 0, 60, 100, 0, 100, 0.0, 100.0},
        MidiNote{1, 1, 60, 100, 0, 100, 0.0, 100.0},
        MidiNote{0, 0, 84, 100, 2400, 100, 2500.0, 100.0}, // 84 dobra para 72
        MidiNote{1, 1, 30, 100, 3840, 100, 4000.0, 100.0}, // 30 dobra para 54
    };

    ImportProfile profile;
    profile.msPerTile = 125;
    profile.gridDivisor = 4;
    profile.tracks = {
        TrackImportSettings{0, true, 0, TrackRole::Melody},
        TrackImportSettings{1, true, 2, TrackRole::Accompaniment},
    };

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.steps.value(0).size(), 1); // dedupe da nota 60
    QCOMPARE(quantized.steps.value(0).first().priority, 0);
    QCOMPARE(quantized.steps.value(20).first().midi, 72);
    QCOMPARE(quantized.steps.value(32).first().midi, 54);
    QCOMPARE(quantized.totalNotes, 3);
}

void TestPktracks::quantizePreservesMelodyContour()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    song.totalMs = 2000.0;
    // Semicolcheias em ticks; msPerTile não define a grade.
    song.notes = {
        MidiNote{0, 0, 65, 100, 0, 100, 0.0, 100.0},
        MidiNote{0, 0, 68, 100, 120, 100, 125.0, 100.0},
        MidiNote{0, 0, 72, 100, 240, 100, 250.0, 100.0},
        MidiNote{0, 0, 76, 100, 360, 100, 375.0, 100.0},
        MidiNote{0, 0, 72, 100, 480, 100, 500.0, 100.0},
        MidiNote{0, 0, 68, 100, 600, 100, 625.0, 100.0},
    };
    ImportProfile profile;
    profile.msPerTile = 214; // playback lento não deve fundir steps
    profile.gridDivisor = 4;
    profile.tracks = {TrackImportSettings{0, true, 0, TrackRole::Melody}};

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.totalNotes, 6);
    const QVector<int> expected = {53, 56, 60, 64, 60, 56};
    for (int i = 0; i < expected.size(); ++i) {
        QCOMPARE(quantized.steps.value(i).first().midi, expected[i]);
    }
}

void TestPktracks::quantizeUsesGlobalTranspose()
{
    MidiSong song;
    song.totalMs = 500.0;
    song.notes = {
        MidiNote{0, 0, 72, 100, 0, 100, 0.0, 100.0}, // +12 → 84 → dobra para 72
    };
    ImportProfile profile;
    profile.msPerTile = 125;
    profile.transpose = 12;
    profile.foldToRange = true;
    profile.tracks = {TrackImportSettings{0, true, 0, TrackRole::Melody}};

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.totalNotes, 1);
    QCOMPARE(quantized.steps.value(0).first().midi, 72);
}

void TestPktracks::quantizeRangesConcatenate()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 120.0;
    song.totalMs = 10000.0;
    // 120 bpm: 1 ms → 0.96 ticks; notas em 0 ms e 8000 ms.
    song.notes = {
        MidiNote{0, 0, 60, 100, 0, 100, 0.0, 100.0},
        MidiNote{0, 0, 62, 100, 4800, 100, 5000.0, 100.0}, // fora dos trechos
        MidiNote{0, 0, 64, 100, 7680, 100, 8000.0, 100.0},
    };
    ImportProfile profile;
    profile.msPerTile = 100;
    profile.gridDivisor = 4;
    profile.tracks = {TrackImportSettings{0, true, 0, TrackRole::Melody}};
    profile.ranges = {{0.0, 1000.0}, {7900.0, 9000.0}};

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.totalNotes, 2);
    QVERIFY(quantized.steps.contains(0));
    // 1º trecho: 0..960 ticks → 8 steps; 2º: tick 7680 com âncora 7584 → step local 1
    QVERIFY(quantized.steps.contains(8 + 1));
}

void TestPktracks::quantizeMsPerTileDoesNotChangeSteps()
{
    MidiSong song;
    song.ppq = 480;
    song.initialBpm = 104.0;
    song.totalMs = 2000.0;
    for (int i = 0; i < 8; ++i) {
        const long tick = i * 120L; // semicolcheias
        song.notes.push_back(MidiNote{0, 0, 72 - i, 100, tick, 60, i * 144.0, 60.0});
    }
    ImportProfile a;
    a.gridDivisor = 4;
    a.msPerTile = 144;
    a.tracks = {TrackImportSettings{0, true, 0, TrackRole::Melody}};
    ImportProfile b = a;
    b.msPerTile = 214;

    const QuantizedSong qa = quantizeSong(song, a);
    const QuantizedSong qb = quantizeSong(song, b);
    QCOMPARE(qa.totalNotes, qb.totalNotes);
    QCOMPARE(qa.lastStep, qb.lastStep);
    for (auto it = qa.steps.constBegin(); it != qa.steps.constEnd(); ++it) {
        QCOMPARE(qb.steps.value(it.key()).size(), it.value().size());
        if (!it.value().isEmpty()) {
            QCOMPARE(qb.steps.value(it.key()).first().midi, it.value().first().midi);
        }
    }
}

void TestPktracks::quantizationPreviewStats()
{
    QuantizedSong song;
    song.lastStep = 4;
    song.steps.insert(0, {melodicNote(60), melodicNote(64), melodicNote(67)});
    song.steps.insert(2, {melodicNote(62)});
    song.steps.insert(4, {
        melodicNote(48), melodicNote(50), melodicNote(52),
        melodicNote(53), melodicNote(55), melodicNote(57), melodicNote(59),
    });
    song.totalNotes = 3 + 1 + 7;
    song.dropped = {
        DroppedNote{1, 72, "acima do máximo de notas por step"},
        DroppedNote{3, 74, "fora do range C3..C#5"},
    };

    const QuantizationPreviewStats stats = buildQuantizationPreviewStats(song, 125, 6);
    QCOMPARE(stats.stepsWithNotes, 3);
    QCOMPARE(stats.tileCount, 5);
    QCOMPARE(stats.totalNotes, 11);
    QCOMPARE(stats.droppedNotes, 2);
    QCOMPARE(stats.maxPolyphony, 7);
    QCOMPARE(stats.stepsOverCap, 1);
    QCOMPARE(stats.durationSeconds, 0.625);
    QCOMPARE(stats.notesPerStep, (QVector<int>{3, 0, 1, 0, 7}));
    QCOMPARE(stats.dropsByReason.value("acima do máximo de notas por step"), 1);
    QCOMPARE(stats.dropsByReason.value("fora do range C3..C#5"), 1);
    QVERIFY(stats.polyStepPercent > 50.0);
    QVERIFY(stats.avgTopMidi > 0.0);
}

void TestPktracks::linearGenerationMatchesSimulation()
{
    QMap<int, QVector<int>> steps;
    for (int s = 0; s <= 20; s += 2) {
        steps[s] = {48 + s};
    }
    const QuantizedSong song = songFromSteps(steps);

    ImportProfile profile;
    profile.geometry = TrackGeometry::Linear;
    profile.msPerTile = 120;

    const GenerationResult result = generateTrack(song, profile);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
    QCOMPARE(result.stats.placedNotes, song.totalNotes);
    QCOMPARE(result.dropped.size(), 0);
    QCOMPARE(result.stats.pinwheels, 0);
    QCOMPARE(countKind(result.layout, ItemKind::Handcar), 1);
}

void TestPktracks::linearPolyphonyUsesPinwheelChain()
{
    // 7 notas num único step: 4 num lado (3 slots + cadeia) e 3 no outro.
    QMap<int, QVector<int>> steps;
    steps[0] = {48, 50, 52, 53, 55, 57, 59};
    const QuantizedSong song = songFromSteps(steps);

    ImportProfile profile;
    profile.geometry = TrackGeometry::Linear;
    profile.maxNotesPerStep = 12;
    profile.maxPinwheelDepth = 1;

    const GenerationResult result = generateTrack(song, profile);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
    QCOMPARE(result.stats.placedNotes, 7);
    QVERIFY(result.stats.pinwheels >= 1);
}

void TestPktracks::pinwheelDepthZeroDropsInsteadOfChaining()
{
    QMap<int, QVector<int>> steps;
    steps[0] = {48, 50, 52, 53, 55, 57, 59, 60};
    const QuantizedSong song = songFromSteps(steps);

    ImportProfile profile;
    profile.geometry = TrackGeometry::Linear;
    profile.maxNotesPerStep = 12;
    profile.maxPinwheelDepth = 0;

    const GenerationResult result = generateTrack(song, profile);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.stats.pinwheels, 0);
    // Cada lado comporta 3 notas (atrás L0, atrás L1, âncora L1) sem cadeia.
    QCOMPARE(result.stats.placedNotes, 6);
    QCOMPARE(result.dropped.size(), 2);
}

void TestPktracks::rectangularGenerationLoops()
{
    QMap<int, QVector<int>> steps;
    for (int s = 0; s < 40; s += 2) {
        steps[s] = {48 + (s % 24)};
    }
    QuantizedSong song = songFromSteps(steps);
    song.lastStep = 39; // período par de 40 steps

    ImportProfile profile;
    profile.geometry = TrackGeometry::Rectangular;
    profile.rectSquareness = 1.0;

    const GenerationResult result = generateTrack(song, profile);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
    QCOMPARE(result.stats.placedNotes, song.totalNotes);
    QCOMPARE(result.stats.pathTiles, 40);

    // O circuito precisa fechar: todo trilho com grau 2.
    GridModel model;
    QString error;
    QVERIFY(buildGridModelFromLayout(result.layout, &model, &error));
    for (const Item& item : model.items()) {
        if (item.kind == ItemKind::Rail) {
            QCOMPARE(model.railDegree({item.anchor, item.layer}), 2);
        }
    }
}

void TestPktracks::rectangularSquarenessChangesShape()
{
    QMap<int, QVector<int>> steps;
    for (int s = 0; s < 60; s += 3) {
        steps[s] = {50};
    }
    QuantizedSong song = songFromSteps(steps);
    song.lastStep = 59;

    ImportProfile squareProfile;
    squareProfile.geometry = TrackGeometry::Rectangular;
    squareProfile.rectSquareness = 1.0;
    const GenerationResult squareResult = generateTrack(song, squareProfile);
    QVERIFY2(squareResult.ok, qPrintable(squareResult.error));

    ImportProfile flatProfile;
    flatProfile.geometry = TrackGeometry::Rectangular;
    flatProfile.rectSquareness = 0.0;
    const GenerationResult flatResult = generateTrack(song, flatProfile);
    QVERIFY2(flatResult.ok, qPrintable(flatResult.error));

    const double squareRatio = double(squareResult.stats.width) / squareResult.stats.height;
    const double flatRatio = double(flatResult.stats.width) / flatResult.stats.height;
    QVERIFY(flatRatio > squareRatio);
}

void TestPktracks::zigzagGenerationMatchesSimulation()
{
    QMap<int, QVector<int>> steps;
    for (int s = 0; s <= 180; s += 2) {
        steps[s] = {48 + (s % 26)};
        if (s % 8 == 0) {
            steps[s].push_back(48 + ((s + 7) % 26));
        }
    }
    const QuantizedSong song = songFromSteps(steps);

    ImportProfile profile;
    profile.geometry = TrackGeometry::Zigzag;
    profile.zigzagRowGap = 7;
    profile.zigzagAspect = 1.0;

    const GenerationResult result = generateTrack(song, profile);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
    QVERIFY(result.stats.rows >= 2);
    QVERIFY(result.stats.placedNotes > 0);
    // Zigzag deve conseguir colocar quase tudo; tolera poucos descartes.
    QVERIFY2(result.stats.placedNotes >= song.totalNotes * 9 / 10,
        qPrintable(QString("colocadas %1 de %2").arg(result.stats.placedNotes).arg(song.totalNotes)));
}

void TestPktracks::percussionGeneration()
{
    QuantizedSong song;
    for (int s = 0; s <= 16; s += 4) {
        StepNote kick;
        kick.midi = 36;
        kick.kind = ItemKind::BigDrum;
        kick.percussion = true;
        kick.priority = 1;
        StepNote snare;
        snare.midi = 38;
        snare.kind = ItemKind::BooBox;
        snare.percussion = true;
        snare.priority = 1;
        song.steps.insert(s, {melodicNote(60, 0), kick, snare});
        song.lastStep = s;
        song.totalNotes += 3;
    }

    ImportProfile profile;
    profile.geometry = TrackGeometry::Linear;

    const GenerationResult result = generateTrack(song, profile);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
    QCOMPARE(result.stats.placedNotes, song.totalNotes);
    QCOMPARE(result.stats.percussion, 10);
    QCOMPARE(countKind(result.layout, ItemKind::BigDrum), 5);
    QCOMPARE(countKind(result.layout, ItemKind::BooBox), 5);
}

void TestPktracks::midiFileEndToEnd()
{
    // Grava um MIDI real (melodia + bateria) e roda o pipeline completo:
    // leitura, análise, quantização e geração linear.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString midiPath = dir.filePath("song.mid");

    smf::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTracks(2);
    file.addTempo(0, 0, 120.0);
    file.addTrackName(1, 0, "Melody");
    const int melody[] = {60, 62, 64, 65, 67, 69, 71, 72};
    for (int i = 0; i < 8; ++i) {
        file.addNoteOn(1, i * 480, 0, melody[i], 100);
        file.addNoteOff(1, i * 480 + 240, 0, melody[i]);
    }
    file.addTrackName(2, 0, "Drums");
    for (int i = 0; i < 8; i += 2) {
        file.addNoteOn(2, i * 480, 9, 36, 100);
        file.addNoteOff(2, i * 480 + 120, 9, 36);
    }
    file.sortTracks();
    QVERIFY(file.write(midiPath.toStdString()));

    MidiSong song;
    QString error;
    QVERIFY2(song.load(midiPath, &error), qPrintable(error));
    QCOMPARE(song.ppq, 480);
    QCOMPARE(song.initialBpm, 120.0);
    QCOMPARE(song.notes.size(), 12);
    QVERIFY(song.tracks.size() >= 3);
    QCOMPARE(song.tracks[1].name, QString("Melody"));
    QVERIFY(song.tracks[2].isPercussion);
    QCOMPARE(song.tracks[2].suggestedRole, TrackRole::Percussion);

    ImportProfile profile;
    profile.msPerTile = 125; // semínima @ 120 bpm = 500ms = 4 steps
    profile.tracks = {
        TrackImportSettings{1, true, 0, TrackRole::Melody},
        TrackImportSettings{2, true, 1, TrackRole::Percussion},
    };
    profile.geometry = TrackGeometry::Linear;

    const QuantizedSong quantized = quantizeSong(song, profile);
    QCOMPARE(quantized.totalNotes, 12);
    QCOMPARE(quantized.lastStep, 28);

    const GenerationResult result = generateTrack(quantized, profile);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join("; ")));
    QCOMPARE(result.stats.placedNotes, 12);
    QCOMPARE(countKind(result.layout, ItemKind::BigDrum), 4);
}

void TestPktracks::ticoTicoImportMatchesSimplified()
{
    const QStringList candidates = {
        QStringLiteral("D:/Downloads/Telegram Desktop/Tico_Tico_no_fub_for__Hands__1781297373595.mid"),
        QStringLiteral(PKTRACKS_ROOT "/Tico_Tico_no_fub_for__Hands__1781297373595.mid"),
    };
    QString midiPath;
    for (const QString& path : candidates) {
        if (QFile::exists(path)) {
            midiPath = path;
            break;
        }
    }
    if (midiPath.isEmpty()) {
        QSKIP("MIDI do Tico-Tico não encontrado");
    }

    MidiSong song;
    QString error;
    QVERIFY2(song.load(midiPath, &error), qPrintable(error));

    QVector<int> enabled;
    QHash<int, TrackRole> roles;
    for (const MidiTrackInfo& track : song.tracks) {
        if (track.noteCount <= 0 || track.isPercussion) {
            continue;
        }
        enabled.push_back(track.index);
        roles.insert(track.index, track.suggestedRole);
    }
    QVERIFY(enabled.size() >= 4);
    QCOMPARE(suggestTranspose(song.notes, enabled, roles), -4);
    QVERIFY(qAbs(suggestGridBpm(song) - 70.0) < 0.01);
    QCOMPARE(msPerTileFromBpm(70.0, 4), 214);

    ImportProfile profile;
    profile.gridDivisor = 4;
    profile.msPerTile = 214;
    profile.transpose = -4;
    profile.foldToRange = true;
    profile.outerVoicesOnly = true;
    profile.maxNotesPerStep = 4;
    for (int i = 0; i < enabled.size(); ++i) {
        profile.tracks.push_back(
            {enabled[i], true, i, roles.value(enabled[i])});
    }
    const auto unique = suggestUniqueBarRangesMs(song, enabled, 4);
    QVERIFY(!unique.isEmpty());
    for (const auto& range : unique) {
        profile.ranges.push_back({range.first, range.second});
    }

    const QuantizedSong quantized = quantizeSong(song, profile);
    const QVector<int> goldOpen = {72, 70, 68, 67, 65, 64, 67, 68, 70, 72, 72};
    QVector<int> tops;
    int openingDoubles = 0;
    for (int step = 0; step <= quantized.lastStep && tops.size() < goldOpen.size(); ++step) {
        const QVector<StepNote> notes = quantized.steps.value(step);
        if (notes.isEmpty()) {
            continue;
        }
        int top = notes.first().midi;
        int bottom = notes.first().midi;
        for (const StepNote& note : notes) {
            top = std::max(top, note.midi);
            bottom = std::min(bottom, note.midi);
        }
        if (tops.size() < 11 && top != bottom) {
            ++openingDoubles;
        }
        tops.push_back(top);
    }
    QCOMPARE(tops, goldOpen);
    QVERIFY(openingDoubles >= 8);
    QSet<int> first;
    for (const StepNote& note : quantized.steps.value(0)) {
        first.insert(note.midi);
    }
    QVERIFY(first.contains(72));
    QVERIFY(first.contains(48));
    QVERIFY2(quantized.lastStep >= 450 && quantized.lastStep <= 500,
        qPrintable(QString("lastStep=%1 (alvo ~476)").arg(quantized.lastStep)));
}

void TestPktracks::regressionExistingLayouts()
{
    const QStringList files = {
        QStringLiteral(PKTRACKS_ROOT "/vibraphone_loop.pktrack.json"),
        QStringLiteral(PKTRACKS_ROOT "/select8_loop.pktrack.json"),
    };
    for (const QString& file : files) {
        if (!QFile::exists(file)) {
            QSKIP("layout de regressão não encontrado");
        }
        TrackLayout layout;
        QString error;
        QVERIFY2(loadLayoutFile(file, &layout, &error), qPrintable(file + ": " + error));

        GridModel model;
        QVERIFY2(buildGridModelFromLayout(layout, &model, &error), qPrintable(file + ": " + error));

        const LayoutSimulation::Result sim = LayoutSimulation::run(model, layout.initialDirection);
        QVERIFY2(sim.error.isEmpty(), qPrintable(file + ": " + sim.error));
        QVERIFY(!sim.events.isEmpty());
    }
}

namespace {

QVector<int> idsOf(const QVector<const Item*>& items)
{
    QVector<int> ids;
    ids.reserve(items.size());
    for (const Item* item : items) {
        ids.push_back(item->id);
    }
    return ids;
}

QVector<int> bruteForceOccupyingIds(const GridModel& model, const QPoint& cell, int layer)
{
    QVector<int> ids;
    const LayerCell target{cell, layer};
    for (const Item& item : model.items()) {
        if (model.occupiedLayerCells(item).contains(target)) {
            ids.push_back(item.id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool bruteForceHasRail(const GridModel& model, const QPoint& cell, int layer)
{
    for (const Item& item : model.items()) {
        if (item.kind == ItemKind::Rail && item.anchor == cell && item.layer == layer) {
            return true;
        }
    }
    return false;
}

QVector<int> bruteForceLaserIds(const GridModel& model)
{
    QVector<int> ids;
    for (const Item& item : model.items()) {
        if (item.kind == ItemKind::Laser) {
            ids.push_back(item.id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void assertIndexedQueriesMatch(const GridModel& model)
{
    QSet<LayerCell> cells;
    for (const Item& item : model.items()) {
        for (const LayerCell& cell : model.occupiedLayerCells(item)) {
            cells.insert(cell);
            cells.insert({cell.cell + QPoint(1, 0), cell.layer});
            cells.insert({cell.cell + QPoint(0, 1), cell.layer});
        }
    }

    for (const LayerCell& cell : cells) {
        QVector<int> indexed = idsOf(model.itemsOccupying(cell.cell, cell.layer));
        std::sort(indexed.begin(), indexed.end());
        QCOMPARE(indexed, bruteForceOccupyingIds(model, cell.cell, cell.layer));
        QCOMPARE(model.hasRailAt(cell.cell, cell.layer), bruteForceHasRail(model, cell.cell, cell.layer));
    }

    QVector<int> laserIds = idsOf(model.laserSensors());
    std::sort(laserIds.begin(), laserIds.end());
    QCOMPARE(laserIds, bruteForceLaserIds(model));
}

} // namespace

void TestPktracks::gridModelIndexesMatchBruteForce()
{
    GridModel model;
    QVERIFY(model.addItem(ItemKind::Rail, QPoint(0, 0), 0, 0));
    QVERIFY(model.addItem(ItemKind::Rail, QPoint(1, 0), 0, 0));
    QVERIFY(model.addItem(ItemKind::Rail, QPoint(2, 0), 0, 0));
    QVERIFY(model.addItem(ItemKind::MusicLowDo, QPoint(1, 1), 0, 0));
    QVERIFY(model.addItem(ItemKind::Laser, QPoint(1, -1), 0, directionToRotation(Direction::South)));
    QVERIFY(model.addItem(ItemKind::BooBox, QPoint(3, 0), 0, 0));
    QVERIFY(model.addItem(ItemKind::Handcar, QPoint(0, 0), 0, 0));
    assertIndexedQueriesMatch(model);
}

void TestPktracks::gridModelIndexesSurviveMutations()
{
    GridModel model;
    int railId = 0;
    int matId = 0;
    int laserId = 0;
    QVERIFY(model.addItem(ItemKind::Rail, QPoint(0, 0), 0, 0, &railId));
    QVERIFY(model.addItem(ItemKind::Rail, QPoint(1, 0), 0, 0));
    QVERIFY(model.addItem(ItemKind::Rail, QPoint(2, 0), 0, 0));
    QVERIFY(model.addItem(ItemKind::MusicRe, QPoint(1, 1), 0, 0, &matId));
    QVERIFY(model.addItem(ItemKind::Laser, QPoint(1, -1), 0, directionToRotation(Direction::South), &laserId));
    QVERIFY(model.addItem(ItemKind::Handcar, QPoint(0, 0), 0, 0));
    assertIndexedQueriesMatch(model);

    QVERIFY(model.moveItem(matId, QPoint(2, 1)));
    assertIndexedQueriesMatch(model);

    QVERIFY(model.rotateItem(laserId));
    assertIndexedQueriesMatch(model);

    QVERIFY(model.moveItems({matId}, QPoint(0, 1)));
    assertIndexedQueriesMatch(model);

    model.shiftLayersUp(0);
    assertIndexedQueriesMatch(model);

    QVERIFY(model.removeItem(matId));
    assertIndexedQueriesMatch(model);

    GridModel copied = model;
    assertIndexedQueriesMatch(copied);
}

void TestPktracks::playbackHotPathKeepsPace()
{
    const QString path = QStringLiteral(PKTRACKS_ROOT "/canon_rock.pktrack.json");
    if (!QFile::exists(path)) {
        QSKIP("canon_rock.pktrack.json não encontrado");
    }

    TrackLayout layout;
    QString error;
    QVERIFY2(loadLayoutFile(path, &layout, &error), qPrintable(error));

    GridModel model;
    QVERIFY2(buildGridModelFromLayout(layout, &model, &error), qPrintable(error));

    const int msPerTile = std::max(1, layout.msPerTile);
    const int targetSteps = std::max(1, 60000 / msPerTile); // ~60s nominais
    const auto handcar = model.firstHandcarCell();
    QVERIFY(handcar.has_value());

    std::optional<LayerCell> previous;
    LayerCell current = *handcar;
    QVector<int> activeLaserIds;
    int sounds = 0;

    QElapsedTimer timer;
    timer.start();
    for (int step = 0; step < targetSteps; ++step) {
        const auto next = LayoutSimulation::nextRailCell(model, current, previous, layout.initialDirection);
        if (!next.has_value()) {
            break;
        }
        previous = current;
        current = *next;
        LayoutSimulation::triggerLasersForCart(model, current, activeLaserIds, [&](const Item&) {
            ++sounds;
        });
    }
    const qint64 elapsedMs = timer.elapsed();
    const qint64 budgetMs = static_cast<qint64>(targetSteps) * msPerTile;

    QVERIFY2(elapsedMs < budgetMs,
             qPrintable(QString("hot path lento: %1 ms para %2 tiles (orçamento %3 ms, sons=%4)")
                            .arg(elapsedMs)
                            .arg(targetSteps)
                            .arg(budgetMs)
                            .arg(sounds)));
}

QTEST_MAIN(TestPktracks)
#include "tst_pktracks.moc"
