#pragma once

#include "AudioEngine.h"
#include "GridCanvas.h"
#include "GridModel.h"
#include "LayoutSerializer.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonObject>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtCore/Qt>
#include <QtWidgets/QMainWindow>

#include <optional>

class QAction;
class QComboBox;
class QListWidget;
class QScrollArea;
class QSpinBox;

class MainWindow : public QMainWindow {
public:
    MainWindow();

    bool loadLayoutFromPath(const QString& path, QString* error = nullptr);
    bool loadTrackLayout(const TrackLayout& layout, QString* error = nullptr);

private:
    bool installGridModel(GridModel model, const TrackLayout& layout, bool markDirty);
    void createPalette();
    void addPaletteItem(const QString& text, std::optional<ItemKind> kind, const QString& tooltip);
    QString paletteTooltip(ItemKind kind) const;
    void createMenus();
    void createTransportBar();
    void createShortcuts();
    void importMidi();

    Direction selectedDirection() const;
    Direction mapNorth() const;
    void setMapNorth(Direction mapNorth, bool remapMusicMats = true);
    int selectedLayer() const;
    void setCurrentLayer(int layer);
    void addTopLayer();
    void addBottomLayer();
    void syncLayerControls();
    void handleCellClicked(const QPoint& cell, std::optional<ItemKind> tool, Qt::KeyboardModifiers modifiers);
    bool handleItemDragged(int itemId, const QPoint& targetAnchor);
    void handleRectSelectionFinished(const QRect& rect);
    void setSelectedItem(int id);
    void setSelectedItems(const QVector<int>& ids);
    void previewItemById(int id);
    void previewSoundAt(const QPoint& cell);
    void playSoundForItem(const Item& item);
    void deleteSelectedItem();
    void rotateSelectedItem();
    void cutSelection();
    void copySelection();
    void pasteClipboard();
    QRect selectedItemsBounds() const;
    void updatePlayAvailability();
    void updateHeadOnLaserWarnings();
    void updatePaletteCounts();
    void scrollToCartIfOffscreen();

    void play();
    void pause();
    void stop();
    void stepSimulation();
    bool advanceSimulationStep();
    qint64 playbackNowMs() const;
    void scheduleNextStep();
    std::optional<LayerCell> nextRailCell(const LayerCell& current) const;
    void triggerLasersForCart(const LayerCell& cartCell);

    QJsonObject layoutJson() const;
    bool loadLayoutJson(const QJsonObject& root, QString* error);
    void exportLayout();
    void exportMapAsPng();
    void importLayout();

    GridModel m_model;
    GridCanvas* m_canvas = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    AudioEngine m_audio;
    QListWidget* m_palette = nullptr;
    std::optional<ItemKind> m_currentTool;
    int m_currentLayer = 0;
    int m_maxLayer = 0;
    int m_selectedItemId = 0;
    QVector<int> m_selectedItemIds;
    std::optional<QPoint> m_selectionAnchorCell;
    QVector<Item> m_clipboardItems;
    QPoint m_clipboardOrigin;
    QPoint m_nextPasteOrigin;
    std::optional<QPoint> m_pasteTargetCell;
    bool m_hasClipboard = false;

    QAction* m_deleteAction = nullptr;
    QAction* m_rotateAction = nullptr;
    QAction* m_cutAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_triggerCountOverlayAction = nullptr;
    QAction* m_addTopLayerAction = nullptr;
    QAction* m_addBottomLayerAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stopAction = nullptr;
    QComboBox* m_directionCombo = nullptr;
    QComboBox* m_mapNorthCombo = nullptr;
    QSpinBox* m_speedSpin = nullptr;
    QSpinBox* m_layerSpin = nullptr;

    QTimer m_stepTimer;
    QElapsedTimer m_playbackClock;
    qint64 m_nextStepDueMs = 0;
    qint64 m_pausedRemainingMs = -1;
    Direction m_mapNorth = Direction::North;
    std::optional<LayerCell> m_simulationStart;
    std::optional<LayerCell> m_simulationCart;
    std::optional<LayerCell> m_previousCartCell;
    QVector<int> m_activeLaserIds;
};
