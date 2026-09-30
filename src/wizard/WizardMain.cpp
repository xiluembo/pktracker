#include "MidiImportWizard.h"

#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    MidiImportWizard wizard;
    wizard.show();
    return app.exec();
}
