#include <QApplication>
#include "AudioCondition.h"

int main(int argc, char** argv) {
    QApplication a(argc, argv);
    AudioCondition condition;
    condition.show();
    return a.exec();
}
