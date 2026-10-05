#include "MainWindow.h"
#include "wizard/MidiImportWizard.h"

#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <cstdio>
#include <QtWidgets/QDialog>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolButton>

class MobileUiTests : public QObject {
    Q_OBJECT
private slots:
    void narrowLayoutHasReachableControls()
    {
        MainWindow window;
        window.resize(320, 640);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.width(), 320);
        const auto screenshot = qEnvironmentVariable("PKTRACKER_UI_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QVERIFY(window.grab().save(screenshot));
        }
        const auto shell = window.findChild<QWidget*>("mobileShell");
        QVERIFY(shell);
        for (auto* button : shell->findChildren<QAbstractButton*>()) {
            QVERIFY2(button->height() >= 48, qPrintable(button->text()));
            QVERIFY(shell->rect().contains(button->mapTo(shell, QPoint(0, 0))));
            QVERIFY(shell->rect().contains(button->mapTo(shell, button->rect().bottomRight())));
        }
    }
    void choosingToolLeavesPanModeAndClosesPalette()
    {
        MainWindow window;
        window.show();
        auto* pan = window.findChild<QPushButton*>("mobilePanButton");
        auto* canvas = window.findChild<QWidget*>("mobileCanvas");
        auto* tools = window.findChild<QPushButton*>("mobileToolsButton");
        auto* dialog = window.findChild<QDialog*>("mobileTools");
        auto* palette = dialog->findChild<QListWidget*>();
        QVERIFY(pan && canvas && tools && dialog && palette);
        QTest::mouseClick(pan, Qt::LeftButton);
        QVERIFY(pan->isChecked());
        QVERIFY(canvas->testAttribute(Qt::WA_TransparentForMouseEvents));
        QTest::mouseClick(tools, Qt::LeftButton);
        QTRY_VERIFY(dialog->isVisible());
        auto* item = palette->item(1);
        QTest::mouseClick(palette->viewport(), Qt::LeftButton, Qt::NoModifier,
            palette->visualItemRect(item).center());
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(!pan->isChecked());
        QVERIFY(!canvas->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(palette->currentRow(), 1);
    }
    void settingsAndWizardRemainAccessible()
    {
        MainWindow window;
        window.show();
        auto* button = window.findChild<QPushButton*>("mobileSettingsButton");
        auto* dialog = window.findChild<QDialog*>("mobileSettings");
        QVERIFY(button && dialog);
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(dialog->findChildren<QSpinBox*>().size(), 2);
        for (auto* spin : dialog->findChildren<QSpinBox*>()) {
            QVERIFY(spin->isVisible());
            QVERIFY(spin->height() >= 48);
        }
        dialog->reject();
        MidiImportWizard wizard(&window);
        QCOMPARE(wizard.findChildren<QScrollArea*>().size(), wizard.pageIds().size() + 1);
        wizard.setFont(QFont("monospace", 14));
        wizard.resize(320, 640);
        wizard.show();
        QVERIFY(QTest::qWaitForWindowExposed(&wizard));
        QCOMPARE(wizard.width(), 320);
        QCOMPARE(wizard.button(QWizard::BackButton)->width(), 48);
        QCOMPARE(wizard.button(QWizard::CancelButton)->width(), 48);
    }
};
int main(int argc, char** argv)
{
    std::fputs("Initializing Qt mobile UI test application\n", stderr);
    std::fflush(stderr);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        const auto text = message.toLocal8Bit();
        std::fprintf(stderr, "%s\n", text.constData());
        std::fflush(stderr);
    });
    QApplication app(argc, argv);
    std::fputs("Qt application initialized; starting mobile UI tests\n", stderr);
    std::fflush(stderr);
    MobileUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_mobile_ui.moc"
