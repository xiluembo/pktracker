#pragma once

#include "GridModel.h"
#include "generator/ImportProfile.h"
#include "generator/NoteMapping.h"
#include "generator/TrackGenerator.h"
#include "midi/MidiSong.h"

#include <QtWidgets/QWizard>
#include <QtWidgets/QWizardPage>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QTableWidget;
class GridCanvas;
class QuantizationPreviewWidget;

class MidiImportWizard : public QWizard {
public:
    explicit MidiImportWizard(QWidget* parent = nullptr);

    void setEmbedded(bool embedded);
    bool isEmbedded() const;

    void accept() override;

    MidiSong song;
    ImportProfile profile;
    QuantizedSong quantized;
    bool hasQuantized = false;
    GenerationResult result;
    GridModel generatedModel;

private:
    bool m_embedded = false;
};

class FilePage : public QWizardPage {
public:
    explicit FilePage(MidiImportWizard* wizard);

    bool isComplete() const override;
    bool validatePage() override;

private:
    void browse();

    MidiImportWizard* m_wizard = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QLabel* m_summaryLabel = nullptr;
    bool m_loaded = false;
};

class TracksPage : public QWizardPage {
public:
    explicit TracksPage(MidiImportWizard* wizard);

    void initializePage() override;
    bool validatePage() override;

private:
    MidiImportWizard* m_wizard = nullptr;
    QTableWidget* m_table = nullptr;
};

class RangePage : public QWizardPage {
public:
    explicit RangePage(MidiImportWizard* wizard);

    void initializePage() override;
    bool validatePage() override;

private:
    void addRange(double startSeconds, double endSeconds);
    void applyUniqueBars();

    MidiImportWizard* m_wizard = nullptr;
    QTableWidget* m_rangesTable = nullptr;
    QListWidget* m_markersList = nullptr;
    QLabel* m_uniqueHintLabel = nullptr;
};

class SettingsPage : public QWizardPage {
public:
    explicit SettingsPage(MidiImportWizard* wizard);

    void initializePage() override;
    bool validatePage() override;

private:
    void updateGeometryControls();
    void applySuggestedGrid();
    QVector<int> enabledTrackIndexes() const;

    MidiImportWizard* m_wizard = nullptr;
    QSpinBox* m_msPerTileSpin = nullptr;
    QComboBox* m_gridCombo = nullptr;
    QLabel* m_gridHintLabel = nullptr;
    QSpinBox* m_transposeSpin = nullptr;
    QPushButton* m_suggestTransposeButton = nullptr;
    QCheckBox* m_foldCheck = nullptr;
    QCheckBox* m_outerVoicesCheck = nullptr;
    QSpinBox* m_maxNotesSpin = nullptr;
    QCheckBox* m_kickCheck = nullptr;
    QCheckBox* m_snareCheck = nullptr;
    QComboBox* m_geometryCombo = nullptr;
    QSlider* m_squarenessSlider = nullptr;
    QLabel* m_squarenessLabel = nullptr;
    QDoubleSpinBox* m_aspectSpin = nullptr;
    QSpinBox* m_rowGapSpin = nullptr;
    QSpinBox* m_pinwheelDepthSpin = nullptr;
};

class QuantizationPreviewPage : public QWizardPage {
public:
    explicit QuantizationPreviewPage(MidiImportWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private:
    void refreshPreview();

    MidiImportWizard* m_wizard = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QuantizationPreviewWidget* m_heatmap = nullptr;
    QPlainTextEdit* m_reportEdit = nullptr;
    QProgressBar* m_busyBar = nullptr;
    bool m_ready = false;
};

class PreviewPage : public QWizardPage {
public:
    explicit PreviewPage(MidiImportWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private:
    void regenerate();
    void exportLayout();
    void exportAndOpenInPlanner();
    QString saveToFile();
    void syncEmbeddedControls();

    MidiImportWizard* m_wizard = nullptr;
    GridModel m_previewModel;
    GridCanvas* m_canvas = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QLabel* m_statsLabel = nullptr;
    QPlainTextEdit* m_reportEdit = nullptr;
    QPushButton* m_regenerateButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_openButton = nullptr;
    QProgressBar* m_busyBar = nullptr;
    bool m_generated = false;
};
