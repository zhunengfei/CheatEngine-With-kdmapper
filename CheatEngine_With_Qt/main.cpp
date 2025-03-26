#include "CheatEngine_With_Qt.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    CheatEngine_With_Qt w;
    w.show();
    return a.exec();
}
