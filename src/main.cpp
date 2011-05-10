/*
 * Copyright (c) 2010 Petr Mrázek (peterix)
 * See LICENSE for details.
 */

#include <QtGui/QApplication>
#include "mul.h"


int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("MUL");
    mul appGui;
    appGui.show();
    return app.exec();
}
