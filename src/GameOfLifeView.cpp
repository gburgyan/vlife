// GameOfLifeView.cpp - Implementation for the main window

#include "GameOfLifeView.h"
#include <QGraphicsRectItem>
#include <QMouseEvent>
#include <QResizeEvent>

GameOfLifeView::GameOfLifeView(QWidget* parent) 
    : QMainWindow(parent), isRunning(false),
      viewportX(0), viewportY(0), viewportWidth(100), viewportHeight(100),
      cellSize(5) {
    setupUI();
    
    // Initialize the board with some pattern (e.g., a glider)
    std::vector<GameOfLife::CellState> glider = {
        GameOfLife::CellState::DEAD, GameOfLife::CellState::ALIVE, GameOfLife::CellState::DEAD,
        GameOfLife::CellState::DEAD, GameOfLife::CellState::DEAD, GameOfLife::CellState::ALIVE,
        GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE, GameOfLife::CellState::ALIVE
    };
    gameOfLife.setCells(10, 10, 3, 3, glider);
    
    renderBoard();
}

GameOfLifeView::~GameOfLifeView() {
    if (timer->isActive()) {
        timer->stop();
    }
    delete timer;
}

void GameOfLifeView::setupUI() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    
    // Graphics view for the board
    scene = new QGraphicsScene(this);
    view = new QGraphicsView(scene);
    view->setDragMode(QGraphicsView::ScrollHandDrag);
    view->setRenderHint(QPainter::Antialiasing);
    view->viewport()->installEventFilter(this);
    
    // Control panel
    QHBoxLayout* controlLayout = new QHBoxLayout();
    
    runStopButton = new QPushButton("Run", this);
    stepButton = new QPushButton("Step", this);
    
    QLabel* speedLabel = new QLabel("Speed:", this);
    speedSlider = new QSlider(Qt::Horizontal, this);
    speedSlider->setRange(1, 100);
    speedSlider->setValue(50);
    
    QLabel* zoomLabel = new QLabel("Zoom:", this);
    zoomSlider = new QSlider(Qt::Horizontal, this);
    zoomSlider->setRange(1, 20);
    zoomSlider->setValue(5);
    
    controlLayout->addWidget(runStopButton);
    controlLayout->addWidget(stepButton);
    controlLayout->addWidget(speedLabel);
    controlLayout->addWidget(speedSlider);
    controlLayout->addWidget(zoomLabel);
    controlLayout->addWidget(zoomSlider);
    
    mainLayout->addWidget(view);
    mainLayout->addLayout(controlLayout);
    
    // Set up signals and slots
    connect(runStopButton, &QPushButton::clicked, this, &GameOfLifeView::onRunStopClicked);
    connect(stepButton, &QPushButton::clicked, this, &GameOfLifeView::onStepClicked);
    connect(speedSlider, &QSlider::valueChanged, this, &GameOfLifeView::onSpeedChanged);
    connect(zoomSlider, &QSlider::valueChanged, this, &GameOfLifeView::onZoomChanged);
    
    // Set up timer for continuous updates
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &GameOfLifeView::updateSimulation);
    
    // Set up window
    resize(800, 600);
    setWindowTitle("Conway's Game of Life");
}

void GameOfLifeView::renderBoard() {
    scene->clear();
    
    // Get current board state for the visible area
    auto cells = gameOfLife.getCells(viewportX, viewportY, viewportWidth, viewportHeight);
    
    // Draw cells
    for (uint32_t y = 0; y < viewportHeight; ++y) {
        for (uint32_t x = 0; x < viewportWidth; ++x) {
            if (cells[y * viewportWidth + x] == GameOfLife::CellState::ALIVE) {
                QGraphicsRectItem* cellItem = scene->addRect(
                    x * cellSize, y * cellSize, cellSize, cellSize,
                    QPen(Qt::black), QBrush(Qt::black));
            } else {
                // Draw a light gray grid for dead cells
                scene->addRect(
                    x * cellSize, y * cellSize, cellSize, cellSize,
                    QPen(Qt::lightGray), QBrush(Qt::white));
            }
        }
    }
    
    // Set scene rectangle
    scene->setSceneRect(0, 0, viewportWidth * cellSize, viewportHeight * cellSize);
}

void GameOfLifeView::onRunStopClicked() {
    isRunning = !isRunning;
    
    if (isRunning) {
        runStopButton->setText("Stop");
        timer->start(100);  // Update every 100ms
        stepButton->setEnabled(false);
    } else {
        runStopButton->setText("Run");
        timer->stop();
        stepButton->setEnabled(true);
    }
}

void GameOfLifeView::onStepClicked() {
    gameOfLife.runGeneration();
    renderBoard();
}

void GameOfLifeView::onSpeedChanged(int value) {
    if (timer->isActive()) {
        timer->setInterval(1000 / value);  // Convert slider value to milliseconds
    }
}

void GameOfLifeView::onZoomChanged(int value) {
    cellSize = value;
    renderBoard();
}

void GameOfLifeView::updateSimulation() {
    gameOfLife.runGeneration();
    renderBoard();
}

void GameOfLifeView::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    
    // Adjust viewport dimensions based on view size and cell size
    viewportWidth = view->width() / cellSize;
    viewportHeight = view->height() / cellSize;
    
    renderBoard();
}

// Custom event filter to handle mouse clicks
bool GameOfLifeView::eventFilter(QObject* obj, QEvent* event) {
    if (obj == view->viewport() && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        
        // Convert screen coordinates to scene coordinates
        QPointF scenePos = view->mapToScene(mouseEvent->pos());
        
        // Calculate cell coordinates
        uint32_t cellX = static_cast<uint32_t>(scenePos.x() / cellSize) + viewportX;
        uint32_t cellY = static_cast<uint32_t>(scenePos.y() / cellSize) + viewportY;
        
        // Toggle cell state
        GameOfLife::CellState currentState = gameOfLife.getCell(cellX, cellY);
        gameOfLife.setCell(cellX, cellY, 
                           currentState == GameOfLife::CellState::ALIVE 
                           ? GameOfLife::CellState::DEAD 
                           : GameOfLife::CellState::ALIVE);
        
        // Redraw the board
        renderBoard();
        
        return true;
    }
    
    return QMainWindow::eventFilter(obj, event);
}
