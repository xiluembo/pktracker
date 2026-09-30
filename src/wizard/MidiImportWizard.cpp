#include "MidiImportWizard.h"

#include "BusyProgress.h"
#include "GridCanvas.h"
#include "LayoutSerializer.h"
#include "QuantizationPreviewWidget.h"
#include "generator/NoteMapping.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QProcess>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

int defaultPriorityForRole(TrackRole role)
{
    switch (role) {
    case TrackRole::Melody:
        return 0;
    case TrackRole::Percussion:
        return 1;
    case TrackRole::Bass:
        return 2;
    case TrackRole::Accompaniment:
        return 3;
    }
    return 3;
}

QString formatSeconds(double ms)
{
    const int totalSeconds = static_cast<int>(ms / 1000.0);
    return QString("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QChar('0'));
}

} // namespace

MidiImportWizard::MidiImportWizard(QWidget* parent)
    : QWizard(parent)
{
    setWindowTitle("Pokopia MIDI Import Wizard");
    setWizardStyle(QWizard::ModernStyle);
    resize(980, 720);

    addPage(new FilePage(this));
    addPage(new TracksPage(this));
    addPage(new RangePage(this));
    addPage(new SettingsPage(this));
    addPage(new QuantizationPreviewPage(this));
    addPage(new PreviewPage(this));

    setButtonText(QWizard::FinishButton, "Concluir");
    setButtonText(QWizard::NextButton, "Avançar >");
    setButtonText(QWizard::BackButton, "< Voltar");
    setButtonText(QWizard::CancelButton, "Cancelar");
}

void MidiImportWizard::setEmbedded(bool embedded)
{
    m_embedded = embedded;
    setButtonText(QWizard::FinishButton, embedded ? "Aplicar no Planner" : "Concluir");
}

bool MidiImportWizard::isEmbedded() const
{
    return m_embedded;
}

void MidiImportWizard::accept()
{
    if (!result.ok) {
        QMessageBox::warning(this, "Geração incompleta",
            result.error.isEmpty() ? "Gere uma pista válida antes de concluir." : result.error);
        return;
    }
    QWizard::accept();
}

// ---------------------------------------------------------------- FilePage

FilePage::FilePage(MidiImportWizard* wizard)
    : m_wizard(wizard)
{
    setTitle("Arquivo MIDI");
    setSubTitle("Escolha o arquivo MIDI que será adaptado para uma pista.");

    auto* layout = new QVBoxLayout(this);
    auto* row = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText("Caminho do arquivo .mid");
    auto* browseButton = new QPushButton("Procurar...", this);
    row->addWidget(m_pathEdit, 1);
    row->addWidget(browseButton);
    layout->addLayout(row);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summaryLabel, 1);

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        browse();
    });
    connect(m_pathEdit, &QLineEdit::textChanged, this, [this]() {
        m_loaded = false;
        emit completeChanged();
    });
}

void FilePage::browse()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Abrir MIDI", QString(), "MIDI (*.mid *.midi);;Todos os arquivos (*)");
    if (!path.isEmpty()) {
        m_pathEdit->setText(path);
    }
}

bool FilePage::isComplete() const
{
    return !m_pathEdit->text().trimmed().isEmpty();
}

bool FilePage::validatePage()
{
    const QString path = m_pathEdit->text().trimmed();
    if (m_loaded && m_wizard->song.path == path) {
        return true;
    }

    QString error;
    if (!m_wizard->song.load(path, &error)) {
        QMessageBox::warning(this, "Falha na leitura", error);
        return false;
    }
    m_loaded = true;

    // Perfil default a partir da análise.
    m_wizard->profile = ImportProfile();
    for (const MidiTrackInfo& track : m_wizard->song.tracks) {
        if (track.noteCount == 0) {
            continue;
        }
        TrackImportSettings settings;
        settings.trackIndex = track.index;
        settings.enabled = true;
        settings.role = track.suggestedRole;
        settings.priority = defaultPriorityForRole(track.suggestedRole);
        m_wizard->profile.tracks.push_back(settings);
    }
    QVector<int> melodicTracks;
    QHash<int, TrackRole> rolesByTrack;
    for (const TrackImportSettings& settings : m_wizard->profile.tracks) {
        if (settings.enabled && settings.role != TrackRole::Percussion) {
            melodicTracks.push_back(settings.trackIndex);
            rolesByTrack.insert(settings.trackIndex, settings.role);
        }
    }
    m_wizard->profile.transpose = suggestTranspose(
        m_wizard->song.notes, melodicTracks, rolesByTrack);
    QVector<int> enabledTracks;
    for (const TrackImportSettings& settings : m_wizard->profile.tracks) {
        if (settings.enabled) {
            enabledTracks.push_back(settings.trackIndex);
        }
    }
    m_wizard->profile.gridDivisor = suggestGridDivisor(m_wizard->song, enabledTracks);
    const double gridBpm = suggestGridBpm(m_wizard->song);
    m_wizard->profile.msPerTile = msPerTileFromBpm(gridBpm, m_wizard->profile.gridDivisor);
    m_wizard->profile.outerVoicesOnly = songLooksHomophonicDense(
        m_wizard->song, melodicTracks, m_wizard->profile.gridDivisor);
    if (m_wizard->profile.outerVoicesOnly) {
        m_wizard->profile.maxNotesPerStep = std::min(m_wizard->profile.maxNotesPerStep, 4);
    }

    int usableTracks = 0;
    for (const MidiTrackInfo& track : m_wizard->song.tracks) {
        if (track.noteCount > 0) {
            ++usableTracks;
        }
    }
    m_summaryLabel->setText(QString(
        "PPQ: %1\nBPM inicial: %2\nBPM da grade: %3\nCompasso: %4/%5\nDuração: %6\nNotas: %7\nFaixas com notas: %8\n"
        "Mudanças de andamento: %9\nMarcadores: %10")
            .arg(m_wizard->song.ppq)
            .arg(m_wizard->song.initialBpm, 0, 'f', 1)
            .arg(gridBpm, 0, 'f', 1)
            .arg(m_wizard->song.timeSignatureNumerator)
            .arg(m_wizard->song.timeSignatureDenominator)
            .arg(formatSeconds(m_wizard->song.totalMs))
            .arg(m_wizard->song.notes.size())
            .arg(usableTracks)
            .arg(m_wizard->song.tempoChanges.size())
            .arg(m_wizard->song.markers.size()));
    return true;
}

// --------------------------------------------------------------- TracksPage

TracksPage::TracksPage(MidiImportWizard* wizard)
    : m_wizard(wizard)
{
    setTitle("Faixas e instrumentos");
    setSubTitle("Selecione as faixas, o papel musical e a prioridade de cada uma. "
                "Prioridade menor descarta por último quando faltar espaço.");

    auto* layout = new QVBoxLayout(this);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels(
        {"Usar", "Faixa", "Nome", "Papel", "Prioridade", "Notas", "Tessitura", "Acordes"});
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_table);
}

void TracksPage::initializePage()
{
    QVector<const MidiTrackInfo*> tracks;
    for (const MidiTrackInfo& track : m_wizard->song.tracks) {
        if (track.noteCount > 0) {
            tracks.push_back(&track);
        }
    }

    m_table->setRowCount(tracks.size());
    for (int row = 0; row < tracks.size(); ++row) {
        const MidiTrackInfo& track = *tracks[row];
        const TrackImportSettings* settings = nullptr;
        for (const TrackImportSettings& candidate : m_wizard->profile.tracks) {
            if (candidate.trackIndex == track.index) {
                settings = &candidate;
                break;
            }
        }

        auto* enabledCheck = new QCheckBox(m_table);
        enabledCheck->setChecked(settings ? settings->enabled : true);
        auto* checkContainer = new QWidget(m_table);
        auto* checkLayout = new QHBoxLayout(checkContainer);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->addWidget(enabledCheck);
        m_table->setCellWidget(row, 0, checkContainer);

        auto* indexItem = new QTableWidgetItem(QString::number(track.index));
        indexItem->setFlags(Qt::ItemIsEnabled);
        indexItem->setData(Qt::UserRole, track.index);
        m_table->setItem(row, 1, indexItem);

        QString name = track.name;
        if (!track.instrumentName.isEmpty()) {
            name += name.isEmpty() ? track.instrumentName : " / " + track.instrumentName;
        }
        if (track.isPercussion) {
            name += name.isEmpty() ? "(canal 10)" : " (canal 10)";
        }
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, 2, nameItem);

        auto* roleCombo = new QComboBox(m_table);
        roleCombo->addItem(trackRoleName(TrackRole::Melody), static_cast<int>(TrackRole::Melody));
        roleCombo->addItem(trackRoleName(TrackRole::Accompaniment), static_cast<int>(TrackRole::Accompaniment));
        roleCombo->addItem(trackRoleName(TrackRole::Bass), static_cast<int>(TrackRole::Bass));
        roleCombo->addItem(trackRoleName(TrackRole::Percussion), static_cast<int>(TrackRole::Percussion));
        const TrackRole role = settings ? settings->role : track.suggestedRole;
        roleCombo->setCurrentIndex(roleCombo->findData(static_cast<int>(role)));
        m_table->setCellWidget(row, 3, roleCombo);

        auto* prioritySpin = new QSpinBox(m_table);
        prioritySpin->setRange(0, 99);
        prioritySpin->setValue(settings ? settings->priority : defaultPriorityForRole(role));
        m_table->setCellWidget(row, 4, prioritySpin);

        auto* notesItem = new QTableWidgetItem(
            QString("%1 (%2/s)").arg(track.noteCount).arg(track.notesPerSecond, 0, 'f', 1));
        notesItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, 5, notesItem);

        auto* rangeItem = new QTableWidgetItem(
            QString("%1..%2").arg(pitchName(track.minMidi), pitchName(track.maxMidi)));
        rangeItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, 6, rangeItem);

        auto* chordItem = new QTableWidgetItem(QString::number(track.maxChordSize));
        chordItem->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, 7, chordItem);
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
}

bool TracksPage::validatePage()
{
    QVector<TrackImportSettings> settings;
    bool anyEnabled = false;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        TrackImportSettings entry;
        entry.trackIndex = m_table->item(row, 1)->data(Qt::UserRole).toInt();
        auto* checkContainer = m_table->cellWidget(row, 0);
        auto* enabledCheck = checkContainer->findChild<QCheckBox*>();
        entry.enabled = enabledCheck && enabledCheck->isChecked();
        auto* roleCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, 3));
        entry.role = static_cast<TrackRole>(roleCombo->currentData().toInt());
        auto* prioritySpin = qobject_cast<QSpinBox*>(m_table->cellWidget(row, 4));
        entry.priority = prioritySpin->value();
        anyEnabled = anyEnabled || entry.enabled;
        settings.push_back(entry);
    }
    if (!anyEnabled) {
        QMessageBox::warning(this, "Nenhuma faixa", "Habilite pelo menos uma faixa.");
        return false;
    }
    m_wizard->profile.tracks = settings;
    m_wizard->hasQuantized = false;
    return true;
}

// ---------------------------------------------------------------- RangePage

RangePage::RangePage(MidiImportWizard* wizard)
    : m_wizard(wizard)
{
    setTitle("Trechos da música");
    setSubTitle("Escolha um ou mais trechos (em segundos) que serão concatenados na pista. "
                "Os marcadores do MIDI podem ajudar a localizar as seções.");

    auto* layout = new QHBoxLayout(this);

    auto* leftLayout = new QVBoxLayout();
    m_rangesTable = new QTableWidget(this);
    m_rangesTable->setColumnCount(2);
    m_rangesTable->setHorizontalHeaderLabels({"Início (s)", "Fim (s)"});
    m_rangesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_rangesTable->verticalHeader()->setVisible(false);
    leftLayout->addWidget(m_rangesTable, 1);

    auto* buttonsRow = new QHBoxLayout();
    auto* addButton = new QPushButton("Adicionar trecho", this);
    auto* removeButton = new QPushButton("Remover selecionado", this);
    auto* uniqueButton = new QPushButton("Só compassos únicos", this);
    uniqueButton->setToolTip(
        "Omite compassos (fórmula do arquivo) que repetem um trecho anterior "
        "(≥90% iguais, reprise ≥8 compassos atrás). Ecos curtos e pontes "
        "ficam; rabos de reprise saem.");
    buttonsRow->addWidget(addButton);
    buttonsRow->addWidget(removeButton);
    buttonsRow->addWidget(uniqueButton);
    buttonsRow->addStretch(1);
    leftLayout->addLayout(buttonsRow);
    m_uniqueHintLabel = new QLabel(this);
    m_uniqueHintLabel->setStyleSheet("color: palette(mid);");
    m_uniqueHintLabel->setWordWrap(true);
    leftLayout->addWidget(m_uniqueHintLabel);
    layout->addLayout(leftLayout, 3);

    auto* rightLayout = new QVBoxLayout();
    rightLayout->addWidget(new QLabel("Marcadores do MIDI:", this));
    m_markersList = new QListWidget(this);
    rightLayout->addWidget(m_markersList, 1);
    layout->addLayout(rightLayout, 2);

    connect(addButton, &QPushButton::clicked, this, [this]() {
        addRange(0.0, m_wizard->song.totalMs / 1000.0);
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int row = m_rangesTable->currentRow();
        if (row >= 0) {
            m_rangesTable->removeRow(row);
        }
    });
    connect(uniqueButton, &QPushButton::clicked, this, [this]() {
        applyUniqueBars();
    });
}

void RangePage::applyUniqueBars()
{
    QVector<int> enabled;
    for (const TrackImportSettings& settings : m_wizard->profile.tracks) {
        if (settings.enabled && settings.role != TrackRole::Percussion) {
            enabled.push_back(settings.trackIndex);
        }
    }
    if (enabled.isEmpty()) {
        for (const TrackImportSettings& settings : m_wizard->profile.tracks) {
            if (settings.enabled) {
                enabled.push_back(settings.trackIndex);
            }
        }
    }
    const int divisor = std::max(1, m_wizard->profile.gridDivisor);
    const auto suggested = suggestUniqueBarRangesMs(m_wizard->song, enabled, divisor);
    if (suggested.isEmpty()) {
        QMessageBox::information(this, "Compassos únicos",
            "Não foi possível detectar compassos para filtrar.");
        return;
    }
    m_rangesTable->setRowCount(0);
    double keptMs = 0.0;
    for (const auto& range : suggested) {
        addRange(range.first / 1000.0, range.second / 1000.0);
        keptMs += range.second - range.first;
    }
    const double total = std::max(1.0, m_wizard->song.totalMs);
    m_uniqueHintLabel->setText(
        QString("%1 trecho(s) sem compassos repetidos (~%2% da duração original).")
            .arg(suggested.size())
            .arg(qRound(100.0 * keptMs / total)));
}

void RangePage::addRange(double startSeconds, double endSeconds)
{
    const int row = m_rangesTable->rowCount();
    m_rangesTable->insertRow(row);
    for (int column = 0; column < 2; ++column) {
        auto* spin = new QDoubleSpinBox(m_rangesTable);
        spin->setDecimals(2);
        spin->setRange(0.0, m_wizard->song.totalMs / 1000.0 + 1.0);
        spin->setValue(column == 0 ? startSeconds : endSeconds);
        m_rangesTable->setCellWidget(row, column, spin);
    }
}

void RangePage::initializePage()
{
    m_rangesTable->setRowCount(0);
    if (m_uniqueHintLabel) {
        m_uniqueHintLabel->clear();
    }
    if (m_wizard->profile.ranges.isEmpty()) {
        addRange(0.0, m_wizard->song.totalMs / 1000.0);
    } else {
        for (const TimeRangeMs& range : m_wizard->profile.ranges) {
            addRange(range.startMs / 1000.0, range.endMs / 1000.0);
        }
    }

    m_markersList->clear();
    for (const MidiMarker& marker : m_wizard->song.markers) {
        m_markersList->addItem(QString("%1  %2").arg(formatSeconds(marker.ms), marker.text));
    }
    if (m_wizard->song.markers.isEmpty()) {
        m_markersList->addItem("(nenhum marcador no arquivo)");
    }
}

bool RangePage::validatePage()
{
    QVector<TimeRangeMs> ranges;
    for (int row = 0; row < m_rangesTable->rowCount(); ++row) {
        auto* startSpin = qobject_cast<QDoubleSpinBox*>(m_rangesTable->cellWidget(row, 0));
        auto* endSpin = qobject_cast<QDoubleSpinBox*>(m_rangesTable->cellWidget(row, 1));
        const double startMs = startSpin->value() * 1000.0;
        const double endMs = endSpin->value() * 1000.0;
        if (endMs <= startMs) {
            QMessageBox::warning(this, "Trecho inválido",
                QString("O trecho da linha %1 termina antes de começar.").arg(row + 1));
            return false;
        }
        ranges.push_back({startMs, endMs});
    }
    if (ranges.isEmpty()) {
        QMessageBox::warning(this, "Nenhum trecho", "Adicione pelo menos um trecho.");
        return false;
    }
    // Trecho único cobrindo tudo equivale à música inteira.
    if (ranges.size() == 1 && ranges[0].startMs <= 0.0 && ranges[0].endMs >= m_wizard->song.totalMs) {
        ranges.clear();
    }
    m_wizard->profile.ranges = ranges;
    m_wizard->hasQuantized = false;
    return true;
}

// -------------------------------------------------------------- SettingsPage

SettingsPage::SettingsPage(MidiImportWizard* wizard)
    : m_wizard(wizard)
{
    setTitle("Parâmetros da pista");
    setSubTitle("Ajuste ritmo, transposição, percussão e a geometria do circuito.");

    auto* layout = new QFormLayout(this);

    auto* gridRow = new QHBoxLayout();
    m_gridCombo = new QComboBox(this);
    m_gridCombo->addItem("Semicolcheia (1/16)", 4);
    m_gridCombo->addItem("Colcheia (1/8)", 2);
    m_gridCombo->addItem("Tercina de colcheia (1/12)", 3);
    m_gridCombo->addItem("Fusa (1/32)", 8);
    m_gridHintLabel = new QLabel(this);
    m_gridHintLabel->setStyleSheet("color: palette(mid);");
    gridRow->addWidget(m_gridCombo);
    gridRow->addWidget(m_gridHintLabel, 1);
    layout->addRow("Grade rítmica:", gridRow);

    m_msPerTileSpin = new QSpinBox(this);
    m_msPerTileSpin->setRange(1, 5000);
    m_msPerTileSpin->setSuffix(" ms");
    m_msPerTileSpin->setToolTip(
        "Velocidade de playback (ms por trilho). A grade musical vem dos ticks "
        "do MIDI + divisor; mudar isto não funde nem espalha notas. "
        "Sugestão: 60000/BPM/divisor (BPM da grade).");
    layout->addRow("Tempo por tile (playback):", m_msPerTileSpin);

    auto* transposeRow = new QHBoxLayout();
    m_transposeSpin = new QSpinBox(this);
    m_transposeSpin->setRange(-48, 48);
    m_suggestTransposeButton = new QPushButton("Sugerir", this);
    m_suggestTransposeButton->setToolTip(
        "Maximiza notas no range com peso Melodia>Baixo>Acompanhamento, "
        "privilegiando a melodia no terço agudo (61..73).");
    transposeRow->addWidget(m_transposeSpin);
    transposeRow->addWidget(m_suggestTransposeButton);
    transposeRow->addStretch(1);
    layout->addRow("Transposição (semitons):", transposeRow);

    m_foldCheck = new QCheckBox("Dobrar oitavas para caber em C3..C#5", this);
    m_foldCheck->setChecked(true);
    m_foldCheck->setToolTip(
        "Desloca a frase inteira em oitavas quando isso faz todas as notas caberem; "
        "o que ainda fica fora é dobrado escolhendo a oitava que preserva o contorno da melodia.");
    layout->addRow(QString(), m_foldCheck);

    m_outerVoicesCheck = new QCheckBox("Só melodia e baixo (agudo + grave por step)", this);
    m_outerVoicesCheck->setToolTip(
        "Descarta vozes internas no MIDI cru (antes da dobra): em cada step "
        "ficam o agudo da Melodia e o grave do Baixo (piano RH/LH). "
        "Dobras de oitava (±12/±24) são colapsadas. O grave dobra rumo ao C3.");
    layout->addRow(QString(), m_outerVoicesCheck);

    m_maxNotesSpin = new QSpinBox(this);
    m_maxNotesSpin->setRange(1, 12);
    layout->addRow("Máximo de notas por step:", m_maxNotesSpin);

    m_kickCheck = new QCheckBox("Bumbo (GM 35/36) vira Big drum", this);
    m_snareCheck = new QCheckBox("Caixa (GM 38/40) vira Boo-in-the-box", this);
    layout->addRow("Percussão:", m_kickCheck);
    layout->addRow(QString(), m_snareCheck);

    m_geometryCombo = new QComboBox(this);
    m_geometryCombo->addItem("Linear", static_cast<int>(TrackGeometry::Linear));
    m_geometryCombo->addItem("Loop retangular", static_cast<int>(TrackGeometry::Rectangular));
    m_geometryCombo->addItem("Zigzag", static_cast<int>(TrackGeometry::Zigzag));
    layout->addRow("Geometria:", m_geometryCombo);

    auto* squarenessRow = new QHBoxLayout();
    m_squarenessSlider = new QSlider(Qt::Horizontal, this);
    m_squarenessSlider->setRange(0, 100);
    m_squarenessSlider->setValue(100);
    m_squarenessLabel = new QLabel("quadrado", this);
    squarenessRow->addWidget(m_squarenessSlider, 1);
    squarenessRow->addWidget(m_squarenessLabel);
    layout->addRow("Formato do retângulo:", squarenessRow);

    m_aspectSpin = new QDoubleSpinBox(this);
    m_aspectSpin->setRange(0.3, 4.0);
    m_aspectSpin->setSingleStep(0.1);
    m_aspectSpin->setValue(1.0);
    layout->addRow("Proporção do zigzag (L/A):", m_aspectSpin);

    m_rowGapSpin = new QSpinBox(this);
    m_rowGapSpin->setRange(5, 12);
    m_rowGapSpin->setValue(7);
    layout->addRow("Espaço entre fileiras do zigzag:", m_rowGapSpin);

    m_pinwheelDepthSpin = new QSpinBox(this);
    m_pinwheelDepthSpin->setRange(0, 4);
    m_pinwheelDepthSpin->setValue(1);
    m_pinwheelDepthSpin->setToolTip(
        "Os 3 níveis do laser são usados primeiro; pinwheels só entram quando esgotam. 0 proíbe pinwheels.");
    layout->addRow("Aninhamento máximo de pinwheels:", m_pinwheelDepthSpin);

    connect(m_gridCombo, &QComboBox::currentIndexChanged, this, [this]() {
        const int divisor = m_gridCombo->currentData().toInt();
        const double gridBpm = suggestGridBpm(m_wizard->song);
        m_msPerTileSpin->setValue(msPerTileFromBpm(gridBpm, divisor));
        if (m_gridHintLabel) {
            m_gridHintLabel->setText(
                QString("playback · BPM %1").arg(gridBpm, 0, 'f', 1));
        }
    });
    connect(m_suggestTransposeButton, &QPushButton::clicked, this, [this]() {
        QVector<int> melodicTracks;
        QHash<int, TrackRole> rolesByTrack;
        for (const TrackImportSettings& settings : m_wizard->profile.tracks) {
            if (settings.enabled && settings.role != TrackRole::Percussion) {
                melodicTracks.push_back(settings.trackIndex);
                rolesByTrack.insert(settings.trackIndex, settings.role);
            }
        }
        m_transposeSpin->setValue(
            suggestTranspose(m_wizard->song.notes, melodicTracks, rolesByTrack));
    });
    connect(m_geometryCombo, &QComboBox::currentIndexChanged, this, [this]() {
        updateGeometryControls();
    });
    connect(m_squarenessSlider, &QSlider::valueChanged, this, [this](int value) {
        m_squarenessLabel->setText(value >= 66 ? "quadrado" : value >= 33 ? "intermediário" : "achatado");
    });
}

void SettingsPage::updateGeometryControls()
{
    const auto geometry = static_cast<TrackGeometry>(m_geometryCombo->currentData().toInt());
    m_squarenessSlider->setEnabled(geometry == TrackGeometry::Rectangular);
    m_aspectSpin->setEnabled(geometry == TrackGeometry::Zigzag);
    m_rowGapSpin->setEnabled(geometry == TrackGeometry::Zigzag);
}

QVector<int> SettingsPage::enabledTrackIndexes() const
{
    QVector<int> enabledTracks;
    for (const TrackImportSettings& settings : m_wizard->profile.tracks) {
        if (settings.enabled) {
            enabledTracks.push_back(settings.trackIndex);
        }
    }
    return enabledTracks;
}

void SettingsPage::applySuggestedGrid()
{
    const int divisor = suggestGridDivisor(m_wizard->song, enabledTrackIndexes());
    const double gridBpm = suggestGridBpm(m_wizard->song);
    m_wizard->profile.gridDivisor = divisor;
    m_wizard->profile.msPerTile = msPerTileFromBpm(gridBpm, divisor);

    {
        const QSignalBlocker blocker(m_gridCombo);
        const int index = m_gridCombo->findData(divisor);
        if (index >= 0) {
            m_gridCombo->setCurrentIndex(index);
        }
    }
    m_msPerTileSpin->setValue(m_wizard->profile.msPerTile);
    m_gridHintLabel->setText(
        QString("sugerida · BPM %1 · %2 ms/tile")
            .arg(gridBpm, 0, 'f', 1)
            .arg(m_wizard->profile.msPerTile));
}

void SettingsPage::initializePage()
{
    const ImportProfile& profile = m_wizard->profile;
    m_transposeSpin->setValue(profile.transpose);
    m_foldCheck->setChecked(profile.foldToRange);
    m_outerVoicesCheck->setChecked(profile.outerVoicesOnly);
    m_maxNotesSpin->setValue(profile.maxNotesPerStep);
    m_kickCheck->setChecked(profile.mapKick);
    m_snareCheck->setChecked(profile.mapSnare);
    m_geometryCombo->setCurrentIndex(
        m_geometryCombo->findData(static_cast<int>(profile.geometry)));
    m_squarenessSlider->setValue(static_cast<int>(profile.rectSquareness * 100));
    m_aspectSpin->setValue(profile.zigzagAspect);
    m_rowGapSpin->setValue(profile.zigzagRowGap);
    m_pinwheelDepthSpin->setValue(profile.maxPinwheelDepth);
    updateGeometryControls();

    // Auto-sugere a grade com base nas faixas atualmente habilitadas.
    applySuggestedGrid();
}

bool SettingsPage::validatePage()
{
    ImportProfile& profile = m_wizard->profile;
    profile.gridDivisor = m_gridCombo->currentData().toInt();
    profile.msPerTile = m_msPerTileSpin->value();
    profile.transpose = m_transposeSpin->value();
    profile.foldToRange = m_foldCheck->isChecked();
    profile.outerVoicesOnly = m_outerVoicesCheck->isChecked();
    profile.maxNotesPerStep = m_maxNotesSpin->value();
    profile.mapKick = m_kickCheck->isChecked();
    profile.mapSnare = m_snareCheck->isChecked();
    profile.geometry = static_cast<TrackGeometry>(m_geometryCombo->currentData().toInt());
    profile.rectSquareness = m_squarenessSlider->value() / 100.0;
    profile.zigzagAspect = m_aspectSpin->value();
    profile.zigzagRowGap = m_rowGapSpin->value();
    profile.maxPinwheelDepth = m_pinwheelDepthSpin->value();
    m_wizard->hasQuantized = false;
    return true;
}

// ---------------------------------------------------- QuantizationPreviewPage

QuantizationPreviewPage::QuantizationPreviewPage(MidiImportWizard* wizard)
    : m_wizard(wizard)
{
    setTitle("Prévia rítmica");
    setSubTitle("Confira a quantização (densidade por step e notas descartadas) antes de gerar a pista. "
                "Volte aos parâmetros se quiser ajustar grade, tempo ou polifonia.");

    auto* layout = new QVBoxLayout(this);
    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);
    m_busyBar->setTextVisible(true);
    m_busyBar->setFormat("Quantizando o MIDI…");
    m_busyBar->hide();
    layout->addWidget(m_busyBar);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summaryLabel);

    m_heatmap = new QuantizationPreviewWidget(this);
    layout->addWidget(m_heatmap, 2);

    m_reportEdit = new QPlainTextEdit(this);
    m_reportEdit->setReadOnly(true);
    layout->addWidget(m_reportEdit, 1);
}

void QuantizationPreviewPage::initializePage()
{
    m_ready = false;
    emit completeChanged();
    m_summaryLabel->setText("Quantizando o MIDI e montando a prévia rítmica…");
    m_reportEdit->clear();
    m_busyBar->show();
    QTimer::singleShot(0, this, [this]() {
        refreshPreview();
    });
}

bool QuantizationPreviewPage::isComplete() const
{
    return m_ready;
}

void QuantizationPreviewPage::refreshPreview()
{
    const MidiSong song = m_wizard->song;
    const ImportProfile profile = m_wizard->profile;

    struct Outcome {
        QuantizedSong quantized;
        QuantizationPreviewStats stats;
    };

    const Outcome outcome = runWithBusyProgress<Outcome>(
        wizard(),
        "Prévia rítmica",
        "Quantizando notas e montando o mapa de densidade…",
        [song, profile]() {
            Outcome result;
            result.quantized = quantizeSong(song, profile);
            result.stats = buildQuantizationPreviewStats(
                result.quantized, profile.msPerTile, profile.maxNotesPerStep);
            return result;
        });

    m_wizard->quantized = outcome.quantized;
    m_wizard->hasQuantized = true;
    const QuantizationPreviewStats& stats = outcome.stats;
    m_ready = stats.totalNotes > 0;
    m_busyBar->hide();

    if (!m_ready) {
        m_summaryLabel->setText("Nenhuma nota restante após a quantização. "
                                "Volte e habilite faixas/trechos ou ajuste a grade.");
        m_heatmap->setPreview(stats, m_wizard->profile.maxNotesPerStep);
        m_reportEdit->setPlainText(QString());
        emit completeChanged();
        return;
    }

    m_summaryLabel->setText(QString(
        "Steps com notas: %1 | Tiles: %2 | Notas: %3 | Descartes: %4\n"
        "Polifonia máx.: %5 | Steps polifônicos: %6% | Topo médio: MIDI %7 | "
        "Dobras de oitava: %8\n"
        "Ornamentos removidos: %9 | Pedal/trêmulo fundido: %10 | "
        "Steps acima do teto (%11): %12 | Duração: %13s @ %14 ms/tile")
            .arg(stats.stepsWithNotes)
            .arg(stats.tileCount)
            .arg(stats.totalNotes)
            .arg(stats.droppedNotes)
            .arg(stats.maxPolyphony)
            .arg(stats.polyStepPercent, 0, 'f', 0)
            .arg(stats.avgTopMidi, 0, 'f', 1)
            .arg(stats.octaveDoubleSteps)
            .arg(stats.ornamentDrops)
            .arg(stats.pedalMergeDrops)
            .arg(m_wizard->profile.maxNotesPerStep)
            .arg(stats.stepsOverCap)
            .arg(stats.durationSeconds, 0, 'f', 1)
            .arg(m_wizard->profile.msPerTile));

    m_heatmap->setPreview(stats, m_wizard->profile.maxNotesPerStep);

    QStringList report;
    if (stats.droppedNotes == 0) {
        report << "Nenhuma nota descartada na quantização.";
    } else {
        report << QString("Descartes na quantização (%1):").arg(stats.droppedNotes);
        for (auto it = stats.dropsByReason.constBegin(); it != stats.dropsByReason.constEnd(); ++it) {
            report << QString("  %1x %2").arg(it.value()).arg(it.key());
        }
    }
    m_reportEdit->setPlainText(report.join('\n'));
    emit completeChanged();
}

// -------------------------------------------------------------- PreviewPage

PreviewPage::PreviewPage(MidiImportWizard* wizard)
    : m_wizard(wizard)
{
    setTitle("Preview e exportação");
    setSubTitle("Confira a pista gerada, as estatísticas e as notas descartadas. "
                "Volte para ajustar parâmetros e gere novamente quando quiser.");

    auto* layout = new QVBoxLayout(this);

    auto* buttonsRow = new QHBoxLayout();
    m_regenerateButton = new QPushButton("Gerar novamente", this);
    m_exportButton = new QPushButton("Salvar .pktrack.json...", this);
    m_openButton = new QPushButton("Salvar e abrir no Planner", this);
    buttonsRow->addWidget(m_regenerateButton);
    buttonsRow->addStretch(1);
    buttonsRow->addWidget(m_exportButton);
    buttonsRow->addWidget(m_openButton);
    layout->addLayout(buttonsRow);

    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);
    m_busyBar->setTextVisible(true);
    m_busyBar->setFormat("Gerando a pista…");
    m_busyBar->hide();
    layout->addWidget(m_busyBar);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_statsLabel);

    m_scrollArea = new QScrollArea(this);
    m_canvas = new GridCanvas(&m_previewModel, this);
    m_scrollArea->setWidget(m_canvas);
    m_scrollArea->setWidgetResizable(false);
    layout->addWidget(m_scrollArea, 3);

    m_reportEdit = new QPlainTextEdit(this);
    m_reportEdit->setReadOnly(true);
    layout->addWidget(m_reportEdit, 1);

    connect(m_regenerateButton, &QPushButton::clicked, this, [this]() {
        regenerate();
    });
    connect(m_exportButton, &QPushButton::clicked, this, [this]() {
        exportLayout();
    });
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        exportAndOpenInPlanner();
    });
}

void PreviewPage::syncEmbeddedControls()
{
    m_openButton->setVisible(!m_wizard->isEmbedded());
}

void PreviewPage::initializePage()
{
    syncEmbeddedControls();
    m_generated = false;
    emit completeChanged();
    m_statsLabel->setText("Gerando a pista a partir da prévia rítmica…");
    m_busyBar->show();
    QTimer::singleShot(0, this, [this]() {
        regenerate();
    });
}

bool PreviewPage::isComplete() const
{
    return m_generated;
}

void PreviewPage::regenerate()
{
    m_generated = false;
    emit completeChanged();
    m_regenerateButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    m_openButton->setEnabled(false);
    m_busyBar->show();

    const ImportProfile profile = m_wizard->profile;
    QuantizedSong quantized = m_wizard->hasQuantized ? m_wizard->quantized : QuantizedSong();
    const MidiSong song = m_wizard->song;
    const bool alreadyQuantized = m_wizard->hasQuantized;

    struct Outcome {
        QuantizedSong quantized;
        GenerationResult result;
        GridModel model;
        QString buildError;
    };

    const Outcome outcome = runWithBusyProgress<Outcome>(
        wizard(),
        "Geração da pista",
        "Montando trilhos, lasers e tapetes…",
        [song, profile, quantized, alreadyQuantized]() mutable {
            Outcome out;
            out.quantized = alreadyQuantized ? quantized : quantizeSong(song, profile);
            out.result = generateTrack(out.quantized, profile);
            if (out.result.ok) {
                if (!buildGridModelFromLayout(out.result.layout, &out.model, &out.buildError)) {
                    if (out.buildError.isEmpty()) {
                        out.buildError = "Falha ao montar o modelo da pista.";
                    }
                }
            }
            return out;
        });

    m_wizard->quantized = outcome.quantized;
    m_wizard->hasQuantized = true;
    m_wizard->result = outcome.result;
    const GenerationResult& result = m_wizard->result;
    m_regenerateButton->setEnabled(true);

    m_generated = result.ok;
    m_exportButton->setEnabled(result.ok);
    m_openButton->setEnabled(result.ok && !m_wizard->isEmbedded());

    if (!result.ok) {
        m_busyBar->hide();
        m_statsLabel->setText("Falha na geração: " + result.error);
        m_previewModel.clear();
        m_wizard->generatedModel.clear();
        m_canvas->refreshGeometry();
        m_reportEdit->setPlainText(QString());
        emit completeChanged();
        return;
    }

    const GenerationStats& stats = result.stats;
    m_statsLabel->setText(QString(
        "Caminho: %1 tiles (%2x%3) | Trilhos: %4 | Lasers: %5 | Tapetes: %6 | "
        "Percussão: %7 | Pinwheels: %8\nNotas colocadas: %9/%10 | Descartadas: %11 | "
        "Disparos simulados: %12 | Duração: %13s @ %14 ms/tile")
            .arg(stats.pathTiles)
            .arg(stats.width)
            .arg(stats.height)
            .arg(stats.rails)
            .arg(stats.lasers)
            .arg(stats.mats)
            .arg(stats.percussion)
            .arg(stats.pinwheels)
            .arg(stats.placedNotes)
            .arg(stats.totalNotes)
            .arg(result.dropped.size())
            .arg(stats.simulatedEvents)
            .arg(stats.durationSeconds, 0, 'f', 1)
            .arg(m_wizard->profile.msPerTile));

    if (outcome.buildError.isEmpty()) {
        m_previewModel = std::move(outcome.model);
        m_wizard->generatedModel = m_previewModel;
    } else {
        m_previewModel.clear();
        m_wizard->generatedModel.clear();
    }
    m_canvas->setCurrentLayer(0);
    m_canvas->setHeadOnLaserIds(m_previewModel.headOnLaserIds(result.layout.initialDirection));
    m_canvas->refreshGeometry();

    QStringList report;
    for (const QString& warning : result.warnings) {
        report << "AVISO: " + warning;
    }
    if (!result.dropped.isEmpty()) {
        report << QString("Notas descartadas (%1):").arg(result.dropped.size());
        QMap<QString, int> byReason;
        for (const DroppedNote& note : result.dropped) {
            ++byReason[note.reason];
        }
        for (auto it = byReason.constBegin(); it != byReason.constEnd(); ++it) {
            report << QString("  %1x %2").arg(it.value()).arg(it.key());
        }
        const int detailLimit = 50;
        int listed = 0;
        for (const DroppedNote& note : result.dropped) {
            if (listed >= detailLimit) {
                report << QString("  ... e mais %1").arg(result.dropped.size() - detailLimit);
                break;
            }
            report << QString("  step %1: %2 (%3)").arg(note.step).arg(pitchName(note.midi), note.reason);
            ++listed;
        }
    }
    if (report.isEmpty()) {
        report << "Nenhum aviso: todas as notas foram colocadas e a simulação confere.";
    }
    m_reportEdit->setPlainText(report.join('\n'));
    m_busyBar->hide();

    emit completeChanged();
}

QString PreviewPage::saveToFile()
{
    QString suggested;
    if (!m_wizard->song.path.isEmpty()) {
        const QFileInfo info(m_wizard->song.path);
        suggested = info.absolutePath() + "/" + info.completeBaseName() + ".pktrack.json";
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Salvar pista", suggested, "Pokopia track (*.pktrack.json);;JSON (*.json)");
    if (path.isEmpty()) {
        return QString();
    }
    QString error;
    if (!saveLayoutFile(m_wizard->result.layout, path, &error)) {
        QMessageBox::warning(this, "Falha ao salvar", error);
        return QString();
    }
    return path;
}

void PreviewPage::exportLayout()
{
    const QString path = saveToFile();
    if (!path.isEmpty()) {
        QMessageBox::information(this, "Pista salva", "Layout exportado para:\n" + path);
    }
}

void PreviewPage::exportAndOpenInPlanner()
{
    const QString path = saveToFile();
    if (path.isEmpty()) {
        return;
    }
    const QString plannerPath = QCoreApplication::applicationDirPath() + "/PokopiaTrackPlanner.exe";
    if (!QFileInfo::exists(plannerPath)) {
        QMessageBox::warning(this, "Planner não encontrado",
            "O executável PokopiaTrackPlanner não está na mesma pasta do wizard.\nA pista foi salva em:\n" + path);
        return;
    }
    QProcess::startDetached(plannerPath, {path});
}
