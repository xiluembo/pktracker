#include "MainWindow.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
#ifdef PKTRACKER_MOBILE_UI
    window.showMaximized();
#else
    window.show();
#endif

    const QStringList arguments = QApplication::arguments();
    if (arguments.size() > 1) {
        QString error;
        if (!window.loadLayoutFromPath(arguments.at(1), &error)) {
            QMessageBox::warning(&window, "Import Failed", error);
        }
    }

    return app.exec();
}
