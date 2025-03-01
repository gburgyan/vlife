// main.cpp - Entry point for the application

#include <QApplication>
#include "GameOfLifeView.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    GameOfLifeView mainWindow;
    mainWindow.show();
    
    return app.exec();
}
