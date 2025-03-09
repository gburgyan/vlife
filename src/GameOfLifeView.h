// GameOfLifeView.h - Header for the main window

#pragma once

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include "SimpleGameOfLife.h"

class GameOfLifeView : public QMainWindow {
    Q_OBJECT

public:
    GameOfLifeView(QWidget* parent = nullptr);
    virtual ~GameOfLifeView();

private slots:
    void onRunStopClicked();
    void onStepClicked();
    void onSpeedChanged(int value);
    void onZoomChanged(int value);
    void updateSimulation();

private:
    SimpleGameOfLife gameOfLife;
    QGraphicsScene* scene;
    QGraphicsView* view;
    QPushButton* runStopButton;
    QPushButton* stepButton;
    QSlider* speedSlider;
    QSlider* zoomSlider;
    QTimer* timer;
    bool isRunning;
    
    uint32_t viewportX;
    uint32_t viewportY;
    uint32_t viewportWidth;
    uint32_t viewportHeight;
    int cellSize;
    
    void setupUI();
    void renderBoard();
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
};
