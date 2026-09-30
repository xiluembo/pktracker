#include "MainWindow.h"

#include "AppConstants.h"
#include "BusyProgress.h"
#include "LaserUtils.h"
#include "LayoutSerializer.h"
#include "LayoutSimulator.h"
#include "wizard/MidiImportWizard.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QIODevice>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QSaveFile>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringList>
#include <QtGui/QAction>
#include <QtGui/QKeySequence>
#include <QtGui/QShortcut>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>

#include <algorithm>
#include <limits>

namespace {

QRect inclusiveCellRect(const QPoint& a, const QPoint& b)
{
    return QRect(
        QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
        QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

} // namespace

MainWindow::MainWindow()
    : m_canvas(new GridCanvas(&m_model, this))
    , m_audio(this)
{
    setWindowTitle("Pokopia Music Track Planner");
    resize(1100, 760);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidget(m_canvas);
    m_scrollArea->setWidgetResizable(false);
    setCentralWidget(m_scrollArea);

    createPalette();
    createMenus();
    createTransportBar();
    createShortcuts();

    m_canvas->onCellClicked = [this](const QPoint& cell, std::optional<ItemKind> tool, Qt::KeyboardModifiers modifiers) {
        handleCellClicked(cell, tool, modifiers);
    };
    m_canvas->onItemDragged = [this](int itemId, const QPoint& targetAnchor) {
        return handleItemDragged(itemId, targetAnchor);
    };
    m_canvas->onRectSelectionFinished = [this](const QRect& rect) {
        handleRectSelectionFinished(rect);
    };

    m_stepTimer.setTimerType(Qt::PreciseTimer);
    m_stepTimer.setSingleShot(true);
    connect(&m_stepTimer, &QTimer::timeout, this, [this]() {
        stepSimulation();
    });

    statusBar()->showMessage("Ready.");
    m_canvas->setMapNorth(m_mapNorth);
    updatePlayAvailability();
}

void MainWindow::createPalette()
{
    auto* dock = new QDockWidget("Palette", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_palette = new QListWidget(dock);
    dock->setWidget(m_palette);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    addPaletteItem("Select / Preview", std::nullopt, "Select items. Click sound items to preview them.");
    for (ItemKind kind : {
             ItemKind::MusicLowDo,
             ItemKind::MusicRe,
             ItemKind::MusicMi,
             ItemKind::MusicFa,
             ItemKind::MusicSol,
             ItemKind::MusicLa,
             ItemKind::MusicTi,
             ItemKind::MusicHighDo,
             ItemKind::BooBox,
             ItemKind::BigDrum,
             ItemKind::Rail,
             ItemKind::Handcar,
             ItemKind::Laser,
             ItemKind::Pinwheel,
         }) {
        addPaletteItem(itemDisplayName(kind), kind, paletteTooltip(kind));
    }

    m_palette->setCurrentRow(0);
    connect(m_palette, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item || item->data(Qt::UserRole).toInt() < 0) {
            m_currentTool.reset();
        } else {
            m_currentTool = static_cast<ItemKind>(item->data(Qt::UserRole).toInt());
        }
        m_canvas->setPlacementTool(m_currentTool);
    });
    updatePaletteCounts();
}

void MainWindow::addPaletteItem(const QString& text, std::optional<ItemKind> kind, const QString& tooltip)
{
    auto* item = new QListWidgetItem(text);
    item->setData(Qt::UserRole, kind.has_value() ? static_cast<int>(*kind) : -1);
    item->setToolTip(tooltip);
    if (kind.has_value() && isMusicMat(*kind)) {
        item->setBackground(musicMatColor(*kind).lighter(135));
    }
    m_palette->addItem(item);
}

void MainWindow::updatePaletteCounts()
{
    if (!m_palette) {
        return;
    }

    int counts[static_cast<int>(ItemKind::Pinwheel) + 1] = {};
    for (const Item& item : m_model.items()) {
        ++counts[static_cast<int>(item.kind)];
    }

    for (int row = 0; row < m_palette->count(); ++row) {
        QListWidgetItem* paletteItem = m_palette->item(row);
        const int kindValue = paletteItem->data(Qt::UserRole).toInt();
        if (kindValue < 0) {
            continue;
        }
        const auto kind = static_cast<ItemKind>(kindValue);
        paletteItem->setText(QString("%1 (%2)").arg(itemDisplayName(kind)).arg(counts[kindValue]));
    }
}

QString MainWindow::paletteTooltip(ItemKind kind) const
{
    if (isMusicMat(kind)) {
        QString tooltip = QString("%1. North: %2, West: %3, South: %4, East: %5.")
                              .arg(itemDisplayName(kind))
                              .arg(pitchName(playedMidi(kind, Direction::North)))
                              .arg(pitchName(playedMidi(kind, Direction::West)))
                              .arg(pitchName(playedMidi(kind, Direction::South)))
                              .arg(pitchName(playedMidi(kind, Direction::East)));
        QStringList hints;
        for (Direction direction : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            const QString hint = enharmonicHint(kind, direction);
            if (!hint.isEmpty()) {
                hints.push_back(directionName(direction) + ": " + hint);
            }
        }
        if (!hints.isEmpty()) {
            tooltip += "\nEnharmonic: " + hints.join(" ");
        }
        return tooltip;
    }
    if (kind == ItemKind::BooBox || kind == ItemKind::BigDrum) {
        return "Percussion item. Occupies this layer and the layer above. Click it on the grid to preview its sound.";
    }
    if (kind == ItemKind::Handcar) {
        return "Place on top of a railway track.";
    }
    if (kind == ItemKind::Laser) {
        return "Beam range: 2 tiles. Activation range: 1 orthogonal tile on this layer and adjacent layers.";
    }
    if (kind == ItemKind::Pinwheel) {
        return "Does not make sound. Triggered by laser activation ranges; activates lasers whose beams cover it.";
    }
    return "Rail shape is generated from adjacent railway tracks.";
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu("File");
    QAction* exportAction = fileMenu->addAction("Export...");
    exportAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(exportAction, &QAction::triggered, this, [this]() {
        exportLayout();
    });

    QAction* exportPngAction = fileMenu->addAction("Export Map as PNG...");
    exportPngAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(exportPngAction, &QAction::triggered, this, [this]() {
        exportMapAsPng();
    });

    QAction* importAction = fileMenu->addAction("Import...");
    importAction->setShortcut(QKeySequence("Ctrl+O"));
    connect(importAction, &QAction::triggered, this, [this]() {
        importLayout();
    });

    QAction* importMidiAction = fileMenu->addAction("Import MIDI...");
    importMidiAction->setShortcut(QKeySequence("Ctrl+Shift+I"));
    connect(importMidiAction, &QAction::triggered, this, [this]() {
        importMidi();
    });

    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction("Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu("Edit");
    m_cutAction = editMenu->addAction("Cut");
    m_cutAction->setShortcut(QKeySequence::Cut);
    connect(m_cutAction, &QAction::triggered, this, [this]() {
        cutSelection();
    });

    m_copyAction = editMenu->addAction("Copy");
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, this, [this]() {
        copySelection();
    });

    m_pasteAction = editMenu->addAction("Paste");
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, this, [this]() {
        pasteClipboard();
    });

    QMenu* viewMenu = menuBar()->addMenu("View");
    m_triggerCountOverlayAction = viewMenu->addAction("Show Trigger Counts");
    m_triggerCountOverlayAction->setCheckable(true);
    connect(m_triggerCountOverlayAction, &QAction::toggled, this, [this](bool checked) {
        m_canvas->setTriggerCountOverlayVisible(checked);
        statusBar()->showMessage(checked ? "Trigger count overlay enabled." : "Trigger count overlay disabled.");
    });
}

void MainWindow::createTransportBar()
{
    QToolBar* toolbar = addToolBar("Transport");
    toolbar->setMovable(false);

    m_deleteAction = toolbar->addAction("Delete");
    connect(m_deleteAction, &QAction::triggered, this, [this]() {
        deleteSelectedItem();
    });

    m_rotateAction = toolbar->addAction("Rotate");
    connect(m_rotateAction, &QAction::triggered, this, [this]() {
        rotateSelectedItem();
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Layer:"));
    m_layerSpin = new QSpinBox(toolbar);
    m_layerSpin->setRange(0, 0);
    m_layerSpin->setValue(0);
    toolbar->addWidget(m_layerSpin);
    connect(m_layerSpin, &QSpinBox::valueChanged, this, [this](int layer) {
        setCurrentLayer(layer);
    });
    m_addTopLayerAction = toolbar->addAction("Add Top Layer");
    connect(m_addTopLayerAction, &QAction::triggered, this, [this]() {
        addTopLayer();
    });
    m_addBottomLayerAction = toolbar->addAction("Add Bottom Layer");
    connect(m_addBottomLayerAction, &QAction::triggered, this, [this]() {
        addBottomLayer();
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Initial direction:"));
    m_directionCombo = new QComboBox(toolbar);
    for (Direction direction : {Direction::North, Direction::East, Direction::South, Direction::West}) {
        m_directionCombo->addItem(directionName(direction), directionToRotation(direction));
    }
    toolbar->addWidget(m_directionCombo);
    connect(m_directionCombo, &QComboBox::currentIndexChanged, this, [this]() {
        updatePlayAvailability();
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Map north:"));
    m_mapNorthCombo = new QComboBox(toolbar);
    for (Direction direction : {Direction::North, Direction::East, Direction::South, Direction::West}) {
        m_mapNorthCombo->addItem(directionName(direction), directionToRotation(direction));
    }
    toolbar->addWidget(m_mapNorthCombo);
    connect(m_mapNorthCombo, &QComboBox::currentIndexChanged, this, [this]() {
        const Direction newNorth = directionFromRotation(m_mapNorthCombo->currentData().toInt());
        if (newNorth == m_mapNorth) {
            return;
        }
        if (m_stepTimer.isActive()) {
            const QSignalBlocker blocker(m_mapNorthCombo);
            m_mapNorthCombo->setCurrentIndex(directionToRotation(m_mapNorth));
            statusBar()->showMessage("Stop playback before changing map north.");
            return;
        }
        setMapNorth(newNorth, true);
        updatePlayAvailability();
        statusBar()->showMessage(QString("Map north set to %1. Music mat rotations updated.").arg(directionName(m_mapNorth)));
    });

    toolbar->addWidget(new QLabel("Speed (ms/tile):"));
    m_speedSpin = new QSpinBox(toolbar);
    m_speedSpin->setRange(1, 5000);
    m_speedSpin->setSingleStep(50);
    m_speedSpin->setValue(kDefaultMsPerTile);
    toolbar->addWidget(m_speedSpin);

    toolbar->addSeparator();
    m_playAction = toolbar->addAction("Play");
    connect(m_playAction, &QAction::triggered, this, [this]() {
        play();
    });
    m_pauseAction = toolbar->addAction("Pause");
    connect(m_pauseAction, &QAction::triggered, this, [this]() {
        pause();
    });
    m_stopAction = toolbar->addAction("Stop");
    connect(m_stopAction, &QAction::triggered, this, [this]() {
        stop();
    });
    m_pauseAction->setEnabled(false);
    m_stopAction->setEnabled(false);
}

void MainWindow::createShortcuts()
{
    auto* deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, [this]() {
        deleteSelectedItem();
    });
    auto* rotateShortcut = new QShortcut(QKeySequence("R"), this);
    connect(rotateShortcut, &QShortcut::activated, this, [this]() {
        rotateSelectedItem();
    });
}

Direction MainWindow::selectedDirection() const
{
    return directionFromRotation(m_directionCombo->currentData().toInt());
}

Direction MainWindow::mapNorth() const
{
    return m_mapNorth;
}

void MainWindow::setMapNorth(Direction mapNorth, bool remapMusicMats)
{
    if (mapNorth == m_mapNorth) {
        return;
    }
    if (remapMusicMats) {
        m_model.remapMusicMatRotationsForMapNorth(m_mapNorth, mapNorth);
    }
    m_mapNorth = mapNorth;
    if (m_mapNorthCombo) {
        const QSignalBlocker blocker(m_mapNorthCombo);
        m_mapNorthCombo->setCurrentIndex(directionToRotation(m_mapNorth));
    }
    m_canvas->setMapNorth(m_mapNorth);
}

int MainWindow::selectedLayer() const
{
    return m_currentLayer;
}

void MainWindow::setCurrentLayer(int layer)
{
    const int boundedLayer = std::max(0, std::min(layer, m_maxLayer));
    if (m_currentLayer == boundedLayer) {
        return;
    }
    m_currentLayer = boundedLayer;
    setSelectedItems({});
    m_selectionAnchorCell.reset();
    m_canvas->setCurrentLayer(m_currentLayer);
    syncLayerControls();
    statusBar()->showMessage(QString("Layer %1 selected.").arg(m_currentLayer));
}

void MainWindow::addTopLayer()
{
    if (m_stepTimer.isActive()) {
        statusBar()->showMessage("Stop playback before adding a layer.");
        return;
    }
    ++m_maxLayer;
    syncLayerControls();
    setCurrentLayer(m_maxLayer);
    statusBar()->showMessage(QString("Top layer %1 added.").arg(m_currentLayer));
}

void MainWindow::addBottomLayer()
{
    if (m_stepTimer.isActive()) {
        statusBar()->showMessage("Stop playback before adding a layer.");
        return;
    }
    m_model.shiftLayersUp(0);
    ++m_maxLayer;
    syncLayerControls();
    m_canvas->refreshGeometry();
    setCurrentLayer(0);
    updatePlayAvailability();
    statusBar()->showMessage("Bottom layer 0 added.");
}

void MainWindow::syncLayerControls()
{
    if (!m_layerSpin) {
        return;
    }
    const QSignalBlocker blocker(m_layerSpin);
    m_layerSpin->setRange(0, m_maxLayer);
    m_layerSpin->setValue(m_currentLayer);
}

void MainWindow::handleCellClicked(const QPoint& cell, std::optional<ItemKind> tool, Qt::KeyboardModifiers modifiers)
{
    if (m_stepTimer.isActive()) {
        statusBar()->showMessage("Stop playback before editing.");
        return;
    }

    m_pasteTargetCell = cell;

    if (tool.has_value()) {
        int newId = 0;
        QString reason;
        const int placementRotation = isMusicMat(*tool) ? directionToRotation(m_mapNorth) : 0;
        if (m_model.addItem(*tool, cell, selectedLayer(), placementRotation, &newId, &reason)) {
            setSelectedItem(newId);
            m_canvas->refreshGeometry();
            statusBar()->showMessage(itemDisplayName(*tool) + " placed.");
            previewItemById(newId);
        } else {
            statusBar()->showMessage(reason);
        }
        updatePlayAvailability();
        return;
    }

    if (modifiers & Qt::ShiftModifier) {
        const QPoint anchor = m_selectionAnchorCell.value_or(cell);
        handleRectSelectionFinished(inclusiveCellRect(anchor, cell));
        return;
    }

    const Item* item = m_model.topItemAt(cell, selectedLayer());
    if (modifiers & Qt::ControlModifier) {
        if (!item) {
            if (m_hasClipboard) {
                statusBar()->showMessage(QString("Paste target set to (%1, %2).").arg(cell.x()).arg(cell.y()));
            }
            return;
        }

        QVector<int> ids = m_selectedItemIds;
        if (ids.contains(item->id)) {
            ids.removeAll(item->id);
        } else {
            ids.push_back(item->id);
        }
        m_selectionAnchorCell = cell;
        setSelectedItems(ids);
        statusBar()->showMessage(QString("%1 item(s) selected.").arg(m_selectedItemIds.size()));
        previewSoundAt(cell);
        return;
    }

    m_selectionAnchorCell = cell;
    setSelectedItem(item ? item->id : 0);
    if (!item && m_hasClipboard) {
        statusBar()->showMessage(QString("Paste target set to (%1, %2).").arg(cell.x()).arg(cell.y()));
        return;
    }
    previewSoundAt(cell);
}

bool MainWindow::handleItemDragged(int itemId, const QPoint& targetAnchor)
{
    if (m_stepTimer.isActive()) {
        statusBar()->showMessage("Stop playback before editing.");
        return false;
    }

    const Item* draggedItem = m_model.findItem(itemId);
    if (!draggedItem) {
        return false;
    }

    m_pasteTargetCell = targetAnchor;
    QVector<int> movingIds = m_selectedItemIds.contains(itemId) ? m_selectedItemIds : QVector<int>{itemId};
    const QPoint delta = targetAnchor - draggedItem->anchor;
    QString reason;
    if (!m_model.moveItems(movingIds, delta, &reason)) {
        if (!reason.isEmpty()) {
            statusBar()->showMessage(reason);
        }
        return false;
    }

    setSelectedItems(movingIds);
    m_canvas->refreshGeometry();
    updatePlayAvailability();
    statusBar()->showMessage(movingIds.size() == 1 ? "Item moved." : "Selection moved.");
    return true;
}

void MainWindow::handleRectSelectionFinished(const QRect& rect)
{
    if (m_stepTimer.isActive()) {
        statusBar()->showMessage("Stop playback before editing.");
        return;
    }

    m_pasteTargetCell = rect.normalized().topLeft();
    m_selectionAnchorCell = rect.normalized().topLeft();
    const QVector<int> ids = m_model.itemIdsInRect(rect, selectedLayer());
    setSelectedItems(ids);
    if (ids.isEmpty()) {
        statusBar()->showMessage("No items selected.");
    } else {
        statusBar()->showMessage(QString("%1 item(s) selected.").arg(ids.size()));
    }
}

void MainWindow::setSelectedItem(int id)
{
    setSelectedItems(id == 0 ? QVector<int>{} : QVector<int>{id});
    const Item* item = m_model.findItem(id);
    if (!item) {
        statusBar()->showMessage("No item selected.");
        return;
    }
    QString message = itemDisplayName(item->kind) + " selected.";
    if (isMusicMat(item->kind)) {
        const Direction musical = musicalDirectionFromScreen(item->rotationQuarters, m_mapNorth);
        message += QString(" Sounds: %1.").arg(pitchName(playedMidiForItem(item->kind, item->rotationQuarters, m_mapNorth)));
        const QString hint = enharmonicHint(item->kind, musical);
        if (!hint.isEmpty()) {
            message += " " + hint;
        }
    }
    statusBar()->showMessage(message);
}

void MainWindow::setSelectedItems(const QVector<int>& ids)
{
    m_selectedItemIds.clear();
    for (int id : ids) {
        if (id != 0 && m_model.findItem(id) && !m_selectedItemIds.contains(id)) {
            m_selectedItemIds.push_back(id);
        }
    }
    m_selectedItemId = m_selectedItemIds.isEmpty() ? 0 : m_selectedItemIds.first();
    m_canvas->setSelectedItems(m_selectedItemIds);
}

void MainWindow::previewItemById(int id)
{
    const Item* item = m_model.findItem(id);
    if (!item) {
        return;
    }
    playSoundForItem(*item);
}

void MainWindow::previewSoundAt(const QPoint& cell)
{
    const Item* soundItem = m_model.firstSoundItemAt(cell, selectedLayer());
    if (!soundItem) {
        return;
    }
    playSoundForItem(*soundItem);
}

void MainWindow::playSoundForItem(const Item& item)
{
    if (isMusicMat(item.kind)) {
        m_audio.playMatPitch(
            item.kind,
            musicalDirectionFromScreen(item.rotationQuarters, m_mapNorth));
    } else if (item.kind == ItemKind::BooBox) {
        m_audio.playPercussion(PercussionKind::BooBox);
    } else if (item.kind == ItemKind::BigDrum) {
        m_audio.playPercussion(PercussionKind::BigDrum);
    }
}

void MainWindow::deleteSelectedItem()
{
    if (m_selectedItemIds.isEmpty()) {
        statusBar()->showMessage("No item is selected.");
        return;
    }
    const int deletedCount = m_selectedItemIds.size();
    if (m_model.removeItems(m_selectedItemIds)) {
        setSelectedItems({});
        m_selectionAnchorCell.reset();
        m_canvas->refreshGeometry();
        statusBar()->showMessage(deletedCount == 1 ? "Item deleted." : QString("%1 items deleted.").arg(deletedCount));
        updatePlayAvailability();
    }
}

void MainWindow::rotateSelectedItem()
{
    if (m_selectedItemIds.size() > 1) {
        statusBar()->showMessage("Rotate supports one selected item at a time.");
        return;
    }
    QString reason;
    if (!m_model.rotateItem(m_selectedItemId, &reason)) {
        statusBar()->showMessage(reason);
        return;
    }
    m_canvas->refreshGeometry();
    previewItemById(m_selectedItemId);
    statusBar()->showMessage("Item rotated.");
    updatePlayAvailability();
}

void MainWindow::cutSelection()
{
    if (m_selectedItemIds.isEmpty()) {
        statusBar()->showMessage("No item is selected.");
        return;
    }
    copySelection();
    deleteSelectedItem();
    statusBar()->showMessage("Selection cut.");
}

void MainWindow::copySelection()
{
    if (m_selectedItemIds.isEmpty()) {
        statusBar()->showMessage("No item is selected.");
        return;
    }

    m_clipboardItems.clear();
    for (int id : m_selectedItemIds) {
        if (const Item* item = m_model.findItem(id)) {
            m_clipboardItems.push_back(*item);
        }
    }

    if (m_clipboardItems.isEmpty()) {
        m_hasClipboard = false;
        statusBar()->showMessage("No item is selected.");
        return;
    }

    const QRect bounds = selectedItemsBounds();
    m_clipboardOrigin = bounds.topLeft();
    m_nextPasteOrigin = m_clipboardOrigin + QPoint(1, 1);
    m_pasteTargetCell.reset();
    m_hasClipboard = true;
    statusBar()->showMessage(QString("%1 item(s) copied. Click a grid cell to set the paste target.").arg(m_clipboardItems.size()));
}

void MainWindow::pasteClipboard()
{
    if (!m_hasClipboard || m_clipboardItems.isEmpty()) {
        statusBar()->showMessage("Clipboard is empty.");
        return;
    }

    const QPoint pasteOrigin = m_pasteTargetCell.value_or(m_nextPasteOrigin);
    QVector<Item> pastedItems;
    pastedItems.reserve(m_clipboardItems.size());
    for (const Item& item : m_clipboardItems) {
        Item pasted = item;
        pasted.id = 0;
        pasted.anchor = pasteOrigin + (item.anchor - m_clipboardOrigin);
        pasted.layer = selectedLayer();
        pastedItems.push_back(pasted);
    }

    GridModel nextModel = m_model;
    QVector<int> newIds;
    auto addPasted = [&](bool handcars, QString* reason) {
        for (const Item& item : pastedItems) {
            if ((item.kind == ItemKind::Handcar) != handcars) {
                continue;
            }
            int newId = 0;
            if (!nextModel.addItem(item.kind, item.anchor, item.layer, item.rotationQuarters, &newId, reason)) {
                return false;
            }
            newIds.push_back(newId);
        }
        return true;
    };

    QString reason;
    if (!addPasted(false, &reason) || !addPasted(true, &reason)) {
        statusBar()->showMessage("Cannot paste: " + reason);
        return;
    }

    m_model = nextModel;
    setSelectedItems(newIds);
    m_nextPasteOrigin = pasteOrigin + QPoint(1, 1);
    m_pasteTargetCell = m_nextPasteOrigin;
    m_canvas->refreshGeometry();
    updatePlayAvailability();
    statusBar()->showMessage(QString("%1 item(s) pasted.").arg(newIds.size()));
}

QRect MainWindow::selectedItemsBounds() const
{
    QRect bounds;
    bool first = true;
    for (int id : m_selectedItemIds) {
        const Item* item = m_model.findItem(id);
        if (!item) {
            continue;
        }
        for (const QPoint& cell : m_model.occupiedCells(*item)) {
            if (first) {
                bounds = QRect(cell, cell);
                first = false;
            } else {
                bounds = bounds.united(QRect(cell, cell));
            }
        }
    }
    return bounds;
}

void MainWindow::updatePlayAvailability()
{
    const QString message = m_model.playValidationMessage(selectedDirection());
    m_playAction->setEnabled(message.isEmpty() && !m_stepTimer.isActive());
    if (!message.isEmpty()) {
        statusBar()->showMessage(message);
    }
    updateHeadOnLaserWarnings();
    updatePaletteCounts();
}

void MainWindow::updateHeadOnLaserWarnings()
{
    m_canvas->setHeadOnLaserIds(m_model.headOnLaserIds(selectedDirection()));
}

void MainWindow::play()
{
    const QString message = m_model.playValidationMessage(selectedDirection());
    if (!message.isEmpty()) {
        QMessageBox::warning(this, "Cannot Play", message);
        updatePlayAvailability();
        return;
    }

    const bool resuming = m_simulationCart.has_value();
    if (!resuming) {
        m_simulationStart = m_model.firstHandcarCell();
        m_simulationCart = m_simulationStart;
        m_previousCartCell.reset();
        m_activeLaserIds.clear();
        m_pausedRemainingMs = -1;
        setCurrentLayer(m_simulationCart->layer);
    }

    m_audio.prewarm();
    m_canvas->setSimulationCart(m_simulationCart);

    const int msPerTile = std::max(1, m_speedSpin->value());
    m_playbackClock.start();
    if (resuming && m_pausedRemainingMs >= 0) {
        m_nextStepDueMs = m_pausedRemainingMs;
        m_pausedRemainingMs = -1;
    } else {
        m_nextStepDueMs = msPerTile;
    }

    m_playAction->setEnabled(false);
    m_pauseAction->setEnabled(true);
    m_stopAction->setEnabled(true);
    statusBar()->showMessage("Playing.");
    if (!resuming) {
        triggerLasersForCart(*m_simulationCart);
    }
    scheduleNextStep();
}

void MainWindow::pause()
{
    if (!m_stepTimer.isActive()) {
        return;
    }
    m_pausedRemainingMs = std::max<qint64>(0, m_nextStepDueMs - playbackNowMs());
    m_stepTimer.stop();
    m_playAction->setEnabled(true);
    m_pauseAction->setEnabled(false);
    m_stopAction->setEnabled(true);
    statusBar()->showMessage("Paused.");
}

void MainWindow::stop()
{
    m_stepTimer.stop();
    m_playbackClock.invalidate();
    m_nextStepDueMs = 0;
    m_pausedRemainingMs = -1;
    m_simulationCart.reset();
    m_simulationStart.reset();
    m_previousCartCell.reset();
    m_activeLaserIds.clear();
    m_canvas->setSimulationCart(std::nullopt);
    m_playAction->setEnabled(true);
    m_pauseAction->setEnabled(false);
    m_stopAction->setEnabled(false);
    updatePlayAvailability();
    statusBar()->showMessage("Stopped.");
}

void MainWindow::scrollToCartIfOffscreen()
{
    if (!m_simulationCart.has_value()) {
        return;
    }

    const QRect bounds = m_model.visibleBounds(m_currentLayer);
    const QPoint cell = m_simulationCart->cell;
    const int cartPixelX = (cell.x() - bounds.left()) * kCellSize + kCellSize / 2;
    const int cartPixelY = (cell.y() - bounds.top()) * kCellSize + kCellSize / 2;

    QScrollBar* hBar = m_scrollArea->horizontalScrollBar();
    QScrollBar* vBar = m_scrollArea->verticalScrollBar();
    const int viewW = m_scrollArea->viewport()->width();
    const int viewH = m_scrollArea->viewport()->height();
    const int scrollX = hBar->value();
    const int scrollY = vBar->value();

    const bool offscreen = cartPixelX < scrollX || cartPixelX > scrollX + viewW
        || cartPixelY < scrollY || cartPixelY > scrollY + viewH;

    if (offscreen) {
        hBar->setValue(cartPixelX - viewW / 2);
        vBar->setValue(cartPixelY - viewH / 2);
    }
}

qint64 MainWindow::playbackNowMs() const
{
    return m_playbackClock.isValid() ? m_playbackClock.elapsed() : 0;
}

void MainWindow::scheduleNextStep()
{
    if (!m_simulationCart.has_value()) {
        return;
    }
    const qint64 delayMs = std::max<qint64>(0, m_nextStepDueMs - playbackNowMs());
    m_stepTimer.start(static_cast<int>(std::min<qint64>(delayMs, std::numeric_limits<int>::max())));
}

bool MainWindow::advanceSimulationStep()
{
    if (!m_simulationCart.has_value()) {
        stop();
        return false;
    }

    const std::optional<LayerCell> next = nextRailCell(*m_simulationCart);
    if (!next.has_value()) {
        stop();
        statusBar()->showMessage("Playback stopped at the end of the railway track.");
        return false;
    }

    m_previousCartCell = m_simulationCart;
    m_simulationCart = next;
    if (m_simulationCart->layer != selectedLayer()) {
        setCurrentLayer(m_simulationCart->layer);
    }
    triggerLasersForCart(*m_simulationCart);
    return true;
}

void MainWindow::stepSimulation()
{
    if (!m_simulationCart.has_value()) {
        stop();
        return;
    }

    const int msPerTile = std::max(1, m_speedSpin->value());
    constexpr int kMaxCatchUpSteps = 8;
    int stepsTaken = 0;

    while (playbackNowMs() >= m_nextStepDueMs && stepsTaken < kMaxCatchUpSteps) {
        if (!advanceSimulationStep()) {
            return;
        }
        m_nextStepDueMs += msPerTile;
        ++stepsTaken;
    }

    m_canvas->setSimulationCart(m_simulationCart);
    scrollToCartIfOffscreen();
    scheduleNextStep();
}

std::optional<LayerCell> MainWindow::nextRailCell(const LayerCell& current) const
{
    return LayoutSimulation::nextRailCell(m_model, current, m_previousCartCell, selectedDirection());
}

void MainWindow::triggerLasersForCart(const LayerCell& cartCell)
{
    LayoutSimulation::triggerLasersForCart(m_model, cartCell, m_activeLaserIds, [this](const Item& item) {
        playSoundForItem(item);
    });
}

QJsonObject MainWindow::layoutJson() const
{
    TrackLayout layout;
    layout.msPerTile = m_speedSpin->value();
    layout.initialDirection = selectedDirection();
    layout.mapNorth = m_mapNorth;
    layout.items = m_model.items();
    return layoutToJson(layout);
}

bool MainWindow::loadLayoutJson(const QJsonObject& root, QString* error)
{
    TrackLayout layout;
    if (!layoutFromJson(root, &layout, error)) {
        return false;
    }
    return loadTrackLayout(layout, error);
}

bool MainWindow::loadTrackLayout(const TrackLayout& layout, QString* error)
{
    GridModel nextModel;
    if (!buildGridModelFromLayout(layout, &nextModel, error)) {
        return false;
    }
    return installGridModel(std::move(nextModel), layout, false);
}

bool MainWindow::installGridModel(GridModel model, const TrackLayout& layout, bool markDirty)
{
    stop();
    m_model = std::move(model);
    m_model.setDirty(markDirty);
    m_maxLayer = m_model.maxLayer();
    m_currentLayer = std::min(m_currentLayer, m_maxLayer);
    m_canvas->setCurrentLayer(m_currentLayer);
    syncLayerControls();
    m_speedSpin->setValue(layout.msPerTile);
    m_directionCombo->setCurrentIndex(directionToRotation(layout.initialDirection));
    setMapNorth(layout.mapNorth, false);
    setSelectedItem(0);
    m_canvas->refreshGeometry();
    updatePlayAvailability();
    return true;
}

bool MainWindow::loadLayoutFromPath(const QString& path, QString* error)
{
    TrackLayout layout;
    QString localError;
    if (!loadLayoutFile(path, &layout, &localError)) {
        if (error) {
            *error = localError;
        }
        return false;
    }
    return loadTrackLayout(layout, error);
}

void MainWindow::importMidi()
{
    if (m_stepTimer.isActive()) {
        statusBar()->showMessage("Stop playback before importing MIDI.");
        return;
    }
    if (!m_model.items().isEmpty()) {
        const auto answer = QMessageBox::question(
            this,
            "Import MIDI",
            "O mapa atual será substituído pela pista gerada. Continuar?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    MidiImportWizard wizard(this);
    wizard.setEmbedded(true);
    if (wizard.exec() != QDialog::Accepted || !wizard.result.ok) {
        return;
    }

    QProgressDialog progress("Aplicando a pista no Planner…", QString(), 0, 0, this);
    progress.setWindowTitle("Aplicar no Planner");
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.show();
    QCoreApplication::processEvents();

    QString error;
    GridModel nextModel;
    if (!wizard.generatedModel.items().isEmpty()) {
        nextModel = std::move(wizard.generatedModel);
    } else {
        const TrackLayout layout = wizard.result.layout;
        struct Built {
            GridModel model;
            QString error;
            bool ok = false;
        };
        const Built built = runWithBusyProgress<Built>(
            this,
            "Aplicar no Planner",
            "Montando o mapa da pista gerada…",
            [layout]() {
                Built result;
                result.ok = buildGridModelFromLayout(layout, &result.model, &result.error);
                return result;
            });
        if (!built.ok) {
            progress.close();
            QMessageBox::warning(this, "Falha ao importar MIDI", built.error);
            return;
        }
        nextModel = std::move(built.model);
    }

    installGridModel(std::move(nextModel), wizard.result.layout, true);
    statusBar()->showMessage("Pista importada do MIDI.");
}

void MainWindow::exportLayout()
{
    const QString path = QFileDialog::getSaveFileName(this, "Export Layout", QString(), "Pokopia track (*.pktrack.json);;JSON (*.json)");
    if (path.isEmpty()) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Export Failed", "Could not open the file for writing.");
        return;
    }
    file.write(QJsonDocument(layoutJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        QMessageBox::warning(this, "Export Failed", "Could not save the file.");
        return;
    }
    m_model.setDirty(false);
    statusBar()->showMessage("Layout exported.");
}

void MainWindow::exportMapAsPng()
{
    const QString path = QFileDialog::getSaveFileName(this, "Export Map as PNG", QString(), "PNG Image (*.png)");
    if (path.isEmpty()) {
        return;
    }

    MapRenderOptions options;
    options.showGhostLayer = true;
    options.showLaserHighlights = false;
    options.showSimulationCart = false;
    options.showWarnings = false;
    options.showTriggerCounts = false;
    options.showSelection = false;
    options.showSelectionRectangle = false;
    options.showMapNorth = true;

    const int layerCount = m_maxLayer + 1;
    if (layerCount == 1) {
        const QImage image = m_canvas->renderLayerToImage(0, options);
        if (image.isNull() || !image.save(path, "PNG")) {
            QMessageBox::warning(this, "Export Failed", "Could not save the PNG image.");
            return;
        }
        statusBar()->showMessage("Map exported as PNG.");
        return;
    }

    // Multiple layers: save one PNG per layer using a numbered suffix.
    QFileInfo info(path);
    const QString baseName = info.completeBaseName();
    const QString dir = info.absolutePath();
    const QString suffix = info.suffix().isEmpty() ? QStringLiteral("png") : info.suffix();

    int saved = 0;
    for (int layer = 0; layer <= m_maxLayer; ++layer) {
        const QString layerPath = QStringLiteral("%1/%2_layer%3.%4").arg(dir, baseName).arg(layer).arg(suffix);
        const QImage image = m_canvas->renderLayerToImage(layer, options);
        if (image.isNull() || !image.save(layerPath, "PNG")) {
            QMessageBox::warning(this, "Export Failed", QStringLiteral("Could not save layer %1.").arg(layer));
            return;
        }
        ++saved;
    }
    statusBar()->showMessage(QStringLiteral("Exported %1 map layer(s) as PNG.").arg(saved));
}

void MainWindow::importLayout()
{
    if (m_model.isDirty()) {
        const auto answer = QMessageBox::question(this, "Discard Unsaved Changes?", "Discard unsaved changes and import another layout?");
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    const QString path = QFileDialog::getOpenFileName(this, "Import Layout", QString(), "Pokopia track (*.pktrack.json);;JSON (*.json)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import Failed", "Could not open the file.");
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, "Import Failed", "The file is not valid JSON.");
        return;
    }

    QString error;
    if (!loadLayoutJson(document.object(), &error)) {
        QMessageBox::warning(this, "Import Failed", error);
        return;
    }
    statusBar()->showMessage("Layout imported.");
}





























