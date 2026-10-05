#include "MainWindow.h"

#include <QtGui/QAction>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScroller>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

// Reuse the editor's actions and model; only the presentation is different.
void MainWindow::createMobileUi()
{
    m_canvas->setObjectName("mobileCanvas");
    setWindowTitle("Pokopia Track Planner");
    resize(420, 800);
    setMinimumSize(320, 480);
    setStyleSheet(QStringLiteral(
        "QPushButton, QToolButton { min-height: 48px; padding: 0 8px; }"
        "QComboBox, QSpinBox { min-height: 48px; }"
        "QListWidget::item { min-height: 48px; padding: 4px 8px; }"
        "QMenu::item { min-height: 48px; padding: 0 16px; }"));

    // Move the existing controls out of the desktop toolbar before hiding it.
    auto* settings = new QDialog(this);
    settings->setObjectName("mobileSettings");
    settings->setWindowTitle("Track settings");
    auto* settingsLayout = new QVBoxLayout(settings);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->addRow("Layer", m_layerSpin);
    form->addRow("Initial direction", m_directionCombo);
    form->addRow("Map north", m_mapNorthCombo);
    form->addRow("Time per tile (ms)", m_speedSpin);
    settingsLayout->addLayout(form);
    auto actionButton = [](QAction* action, QWidget* parent) {
        auto* button = new QToolButton(parent);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setAccessibleName(action->text());
        return button;
    };
    settingsLayout->addWidget(actionButton(m_addTopLayerAction, settings));
    settingsLayout->addWidget(actionButton(m_addBottomLayerAction, settings));
    auto* settingsDone = new QPushButton("Done", settings);
    settingsLayout->addWidget(settingsDone);
    connect(settingsDone, &QPushButton::clicked, settings, &QDialog::accept);
    settings->resize(320, 560);

    auto* tools = new QDialog(this);
    tools->setObjectName("mobileTools");
    tools->setWindowTitle("Choose a tool");
    auto* toolsLayout = new QVBoxLayout(tools);
    auto* toolsHint = new QLabel("Choose a tool, then tap the map to place it.", tools);
    toolsHint->setWordWrap(true);
    toolsLayout->addWidget(toolsHint);
    toolsLayout->addWidget(m_palette);
    m_palette->setAccessibleName("Map tools");
    m_palette->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* toolsDone = new QPushButton("Done", tools);
    toolsLayout->addWidget(toolsDone);
    connect(toolsDone, &QPushButton::clicked, tools, &QDialog::accept);
    connect(m_palette, &QListWidget::itemClicked, tools, &QDialog::accept);
    tools->resize(320, 560);

    for (auto* toolbar : findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        removeToolBar(toolbar);
        toolbar->hide();
    }
    for (auto* dock : findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        removeDockWidget(dock);
        dock->hide();
    }
    // Reparented toolbar widgets retain their explicit hidden state.
    m_layerSpin->show();
    m_directionCombo->show();
    m_mapNorthCombo->show();
    m_speedSpin->show();
    m_palette->show();
    // Keep File / Edit / View actions reachable from a single overflow button.
    auto* menu = new QMenu(this);
    for (auto* action : menuBar()->actions()) {
        menu->addAction(action);
    }
    menuBar()->hide();

    auto* shell = new QWidget(this);
    shell->setObjectName("mobileShell");
    auto* layout = new QVBoxLayout(shell);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    auto* top = new QHBoxLayout;
    auto* toolsButton = new QPushButton("Tools", shell);
    toolsButton->setObjectName("mobileToolsButton");
    toolsButton->setAccessibleName("Choose map tool");
    auto* panButton = new QPushButton("Pan", shell);
    panButton->setObjectName("mobilePanButton");
    panButton->setCheckable(true);
    panButton->setAccessibleName("Pan map without editing");
    auto* moreButton = new QToolButton(shell);
    moreButton->setText("Menu");
    moreButton->setAccessibleName("File, edit and view menu");
    moreButton->setMenu(menu);
    moreButton->setPopupMode(QToolButton::InstantPopup);
    top->addWidget(toolsButton, 1);
    top->addWidget(panButton, 1);
    top->addWidget(moreButton);
    layout->addLayout(top);
    auto* hint = new QLabel("Select / Preview · Tap an item to hear it", shell);
    hint->setObjectName("mobileHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto updateHint = [this, hint, panButton]() {
        hint->setText(panButton->isChecked()
            ? "Pan mode · Drag to move the map"
            : m_currentTool.has_value()
                ? itemDisplayName(*m_currentTool) + " · Tap to place"
                : "Select / Preview · Tap to select; drag to move");
    };
    connect(panButton, &QPushButton::toggled, this, [this, updateHint](bool pan) {
        // Gesture capture is exclusive to Pan mode, so editing never scrolls
        // the map or accidentally places an item while panning.
        m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, pan);
        if (pan) {
            QScroller::grabGesture(m_scrollArea->viewport(), QScroller::LeftMouseButtonGesture);
        } else {
            QScroller::ungrabGesture(m_scrollArea->viewport());
        }
        updateHint();
    });
    connect(m_palette, &QListWidget::currentRowChanged, this,
        [panButton, updateHint](int) { panButton->setChecked(false); updateHint(); });
    connect(toolsButton, &QPushButton::clicked, tools, &QDialog::open);

    takeCentralWidget();
    layout->addWidget(m_scrollArea, 1);
    auto* editing = new QHBoxLayout;
    editing->addWidget(actionButton(m_rotateAction, shell));
    editing->addWidget(actionButton(m_deleteAction, shell));
    auto* settingsButton = new QPushButton("Settings", shell);
    settingsButton->setObjectName("mobileSettingsButton");
    editing->addWidget(settingsButton);
    connect(settingsButton, &QPushButton::clicked, settings, &QDialog::open);
    layout->addLayout(editing);
    auto* transport = new QHBoxLayout;
    transport->addWidget(actionButton(m_playAction, shell));
    transport->addWidget(actionButton(m_pauseAction, shell));
    transport->addWidget(actionButton(m_stopAction, shell));
    layout->addLayout(transport);
    setCentralWidget(shell);
    statusBar()->setSizeGripEnabled(false);
}
