//
// Created by Mikhail Vorotnikov on 8/26/24.
//

#include "KinematicVisualizer.h"
#include <QDebug>
#include <QColor>
#include <cstdlib> // For rand() and srand()
#include <ctime>   // For time()
#include "qcustomplot.h"
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QPainter>
#include "label.h"

// Static member variables for color mapping, custom plots, vertical lines, and selection rectangle
QMap<QString, QColor> KinematicVisualizer::colorMap;
QList<QCustomPlot*> KinematicVisualizer::customPlots;
QMap<QCustomPlot*, QCPItemLine*> KinematicVisualizer::vLinesMap;
QCustomPlot* KinematicVisualizer::lastPlotWithLine = nullptr;
QCPItemRect* KinematicVisualizer::selectionRect = nullptr;  // Initialize selection rectangle

// Function to generate a random color within a specific range
QColor KinematicVisualizer::generateRandomColor() {
    const int minColorValue = 80;
    const int maxColorValue = 175;
    int red = minColorValue + rand() % (maxColorValue - minColorValue + 1);
    int green = minColorValue + rand() % (maxColorValue - minColorValue + 1);
    int blue = minColorValue + rand() % (maxColorValue - minColorValue + 1);
    return QColor(red, green, blue);
}

// Constructor
KinematicVisualizer::KinematicVisualizer(QWidget *parent)
        : QWidget(parent), customPlot(new QCustomPlot(this)), selecting(false), label(new Label(customPlot)) {

    // Set up layout
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(customPlot);
    layout->setContentsMargins(0, 0, 0, 0); // Set margins to 0
    layout->setSpacing(0); // Set spacing between widgets to 0
    setLayout(layout);

    // Setup selection rectangle for zoom
    customPlot->setSelectionRectMode(QCP::SelectionRectMode::srmNone);

    // Connect signals and slots
    connect(customPlot, &QCustomPlot::mousePress, this, &KinematicVisualizer::onAnyMousePress);
    connect(customPlot, &QCustomPlot::mouseRelease, this, &KinematicVisualizer::onMouseRelease);
    connect(customPlot, &QCustomPlot::mouseMove, this, &KinematicVisualizer::onMouseDrag);
    connect(customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), this, SLOT(synchronizePlots(QCPRange)));

    // Enable mouse tracking
    setMouseTracking(true);
    customPlot->setMouseTracking(true);

    // Install event filter
    customPlot->installEventFilter(this);

    // Add the custom plot to the list and synchronize Y-axes
    customPlots.append(customPlot);
    setupCursorItems(customPlot);

    // Ensure xAxis2 (top axis) is configured properly
    customPlot->xAxis2->setVisible(false);
    customPlot->xAxis2->setTickLabels(false);
    customPlot->xAxis2->setTicks(false); // Initially disable ticks
    customPlot->xAxis2->setBasePen(Qt::NoPen);
    customPlot->xAxis2->setTickPen(Qt::NoPen);
    customPlot->xAxis2->setSubTickPen(Qt::NoPen);
}

// Function to get the associated label object
Label* KinematicVisualizer::getLabel() const {
    return label;
}

// Function to setup cursor items (vertical and horizontal lines, coordinate text, and frame)
void KinematicVisualizer::setupCursorItems(QCustomPlot *plot) {
    // Create and configure overlay layers if they don't exist
    if (!plot->layer("overlay")) {
        plot->addLayer("overlay", plot->layer("main"), QCustomPlot::limAbove);
    }

    if (!plot->layer("textOverlay")) {
        plot->addLayer("textOverlay", plot->layer("overlay"), QCustomPlot::limAbove);
    }

    // Create and configure the vertical line
    QCPItemLine *vLine = new QCPItemLine(plot);
    vLine->setLayer("overlay");
    vLine->setPen(QPen(Qt::red, 1, Qt::DotLine));
    vLine->start->setType(QCPItemPosition::ptPlotCoords);
    vLine->end->setType(QCPItemPosition::ptPlotCoords);
    vLine->setSelectable(false);
    vLine->setVisible(false);
    vLinesMap[plot] = vLine;

    if (plot == customPlot) {
        // Create and configure the horizontal line, coordinate text, and frame for the main plot
        hLine = new QCPItemLine(plot);
        coordText = new QCPItemText(plot);
        coordFrame = new QCPItemRect(plot);

        // Setup horizontal line
        hLine->setLayer("overlay");
        hLine->setPen(QPen(Qt::red, 1, Qt::DotLine));
        hLine->start->setType(QCPItemPosition::ptPlotCoords);
        hLine->end->setType(QCPItemPosition::ptPlotCoords);
        hLine->setSelectable(false);
        hLine->setVisible(false);

        // Setup coordinate frame
        coordFrame->setLayer("overlay");
        coordFrame->setPen(QPen(Qt::NoPen)); // Remove the frame border
        QColor backgroundColor(255, 0, 0, 50); // Red color with alpha for transparency
        coordFrame->setBrush(QBrush(backgroundColor)); // Frame background color
        coordFrame->setVisible(false);

        // Setup coordinate text
        coordText->setLayer("textOverlay");
        coordText->position->setType(QCPItemPosition::ptPlotCoords);
        coordText->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
        coordText->setFont(QFont(font().family(), 10));
        coordText->setColor(Qt::black);
        coordText->setSelectable(false);
        coordText->setVisible(false);
    }
}

// Event filter for handling mouse move, enter, and leave events
bool KinematicVisualizer::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        cursorPos = mouseEvent->pos();
        if (QCustomPlot *plot = qobject_cast<QCustomPlot*>(object)) {
            updateCursorItems(plot);
        }
        if (selecting && selectionRect) {
            onMouseDrag();  // Update selection rectangle while dragging
        }
        return true;
    } else if (event->type() == QEvent::Leave) {
        hideAllVerticalLines();
        hideHorizontalCursor();
        return true;
    } else if (event->type() == QEvent::Enter) {
        if (QCustomPlot *plot = qobject_cast<QCustomPlot*>(object)) {
            updateCursorItems(plot);
            // Ensure rectangle and text are shown when entering the plot
            coordText->setVisible(true);
            coordFrame->setVisible(true);
        }
        return true;
    }
    return QWidget::eventFilter(object, event);
}

// Update cursor items (vertical line, horizontal line, coordinate text, and frame) based on mouse position
void KinematicVisualizer::updateCursorItems(QCustomPlot *plot) {
    double x = plot->xAxis->pixelToCoord(cursorPos.x());
    double y;

    if (plot->property("isSpectrogram").toBool()) {
        // Use the y-axis position of the mouse for the spectrogram
        y = plot->yAxis->pixelToCoord(cursorPos.y());
    } else {
        // Extract the corresponding Y value from the signal data
        y = getYValueFromSignal(x);
    }

    // Hide all vertical lines
    hideAllVerticalLines();

    // Update vertical line position for the current plot
    QCPItemLine *vLine = vLinesMap[plot];
    vLine->start->setCoords(x, plot->yAxis->range().lower);
    vLine->end->setCoords(x, plot->yAxis->range().upper);
    vLine->setVisible(true);

    // Update horizontal line and coordinate text for the main plot
    if (plot == customPlot) {
        double adjustedY = y;

        if (trackedParameter == "X") {
            adjustedY += signalOffsets.value("X", 0.0);
        } else if (trackedParameter == "Y") {
            adjustedY += signalOffsets.value("Y", 0.0);
        } else if (trackedParameter == "Z") {
            adjustedY += signalOffsets.value("Z", 0.0);
        }

        if (cursorPos.y() >= 0 && cursorPos.y() <= plot->height()) {
            hLine->start->setCoords(plot->xAxis->range().lower, adjustedY);
            hLine->end->setCoords(plot->xAxis->range().upper, adjustedY);
            hLine->setVisible(true);
        } else {
            hLine->setVisible(false);
        }

        // Update coordinate text
        QString coordStr = QString("X: %1\nY: %2").arg(x).arg(y);
        coordText->setText(coordStr);

        // Calculate text bounding rectangle based only on the X-axis value
        QString xCoordStr = QString("X: %1").arg(x);
        QFontMetrics fm(coordText->font());
        QRect textRect = fm.boundingRect(xCoordStr);

        // Update coordinate frame size based on the text width for the X-axis value
        const int padding = 5; // Optional padding to increase frame size
        const int frameWidth = textRect.width() + padding * 2;
        const int frameHeight = fm.height() * 2 + padding;

        coordFrame->topLeft->setPixelPosition(QPoint(cursorPos.x() + 20 - padding, cursorPos.y() - padding));
        coordFrame->bottomRight->setPixelPosition(QPoint(cursorPos.x() + 20 + frameWidth, cursorPos.y() + frameHeight));
        coordFrame->setVisible(true);

        // Update coordinate text position to be inside the rectangle
        coordText->position->setPixelPosition(QPoint(cursorPos.x() + 20, cursorPos.y()));
        coordText->setVisible(true);

        // Replot only the text overlay layer
        customPlot->layer("textOverlay")->replot();
    }

    // Update vertical line in all plots
    updateVerticalLineInAllPlots(x);
}

// Hide all vertical lines in all plots
void KinematicVisualizer::hideAllVerticalLines() {
    for (QCustomPlot *plot : customPlots) {
        QCPItemLine *vLine = vLinesMap[plot];
        if (vLine) {
            vLine->setVisible(false);
        }
    }
}

// Hide the horizontal cursor and coordinate items
void KinematicVisualizer::hideHorizontalCursor() {
    hLine->setVisible(false);
    coordText->setVisible(false);
    coordFrame->setVisible(false);
    customPlot->layer("textOverlay")->replot();
}

// Update the vertical line position in all plots
void KinematicVisualizer::updateVerticalLineInAllPlots(double x) {
    for (QCustomPlot *plot : customPlots) {
        QCPItemLine *vLine = vLinesMap[plot];
        if (vLine) {
            vLine->start->setCoords(x, plot->yAxis->range().lower);
            vLine->end->setCoords(x, plot->yAxis->range().upper);
            vLine->setVisible(true);
            plot->layer("overlay")->replot();
        }
    }
}

// Destructor
KinematicVisualizer::~KinematicVisualizer() {
    customPlots.removeAll(customPlot);
}

// Synchronize the Y-axes of all plots
void KinematicVisualizer::synchronizeYAxes() {
    if (customPlots.size() < 2) return;

    QCustomPlot *referencePlot = customPlots.first();
    for (QCustomPlot *plot : customPlots) {
        if (plot != referencePlot) {
            connect(referencePlot->xAxis, SIGNAL(rangeChanged(QCPRange)), plot->xAxis, SLOT(setRange(QCPRange)));
            connect(plot->xAxis, SIGNAL(rangeChanged(QCPRange)), referencePlot->xAxis, SLOT(setRange(QCPRange)));

            connect(referencePlot->yAxis, SIGNAL(rangeChanged(QCPRange)), plot->yAxis, SLOT(setRange(QCPRange)));
            connect(plot->yAxis, SIGNAL(rangeChanged(QCPRange)), referencePlot->yAxis, SLOT(setRange(QCPRange)));
        }
    }
}

// Mouse move event handler
void KinematicVisualizer::mouseMoveEvent(QMouseEvent *event) {
    if (customPlot->viewport().contains(event->pos())) {
        cursorPos = event->pos(); // Update cursor position
        updateCursorItems(customPlot);
        if (selecting && selectionRect) {
            onMouseDrag();
        }
    }
}

// Function to get Y value from the signal data based on the X value
double KinematicVisualizer::getYValueFromSignal(double x) {
    QVector<QPair<double, double>> *signalData = nullptr;

    if (trackedParameter == "X") {
        signalData = &signalDataX;
    } else if (trackedParameter == "Y") {
        signalData = &signalDataY;
    } else if (trackedParameter == "Z") {
        signalData = &signalDataZ;
    }

    if (!signalData || signalData->isEmpty()) {
        return 0.0;
    }

    auto it = std::lower_bound(signalData->begin(), signalData->end(), x,
                               [](const QPair<double, double> &a, double value) {
                                   return a.first < value;
                               });

    if (it == signalData->end()) {
        return signalData->last().second;
    } else if (it == signalData->begin()) {
        return signalData->first().second;
    } else {
        auto prevIt = std::prev(it);
        double x1 = prevIt->first;
        double y1 = prevIt->second;
        double x2 = it->first;
        double y2 = it->second;

        if (x2 - x1 == 0) {
            return y1;
        } else {
            return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
        }
    }
}

// Paint event handler
void KinematicVisualizer::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
}

// Function to get the custom plot
QCustomPlot* KinematicVisualizer::getCustomPlot() const {
    return customPlot;
}

// Function to get the minimum limit of the X-axis
double KinematicVisualizer::getXAxisMinLimit() const {
    return xAxisMinLimit;
}

// Function to get the maximum limit of the X-axis
double KinematicVisualizer::getXAxisMaxLimit() const {
    return xAxisMaxLimit;
}

// Function to visualize a signal with provided data
void KinematicVisualizer::visualizeSignal(const QMap<QString, QVector<double>> &dataMap, const QString &configName, int penWidth, int samplingRate) {
    setupCustomPlot();
    customPlot->setFixedHeight(150);

    // Setup legend
    customPlot->legend->setVisible(true);
    QFont legendFont = font();
    legendFont.setPointSize(10);
    customPlot->legend->setIconSize(10, 10); // Set legend icon size
    customPlot->legend->setFont(legendFont);
    customPlot->legend->setBrush(QBrush(QColor(255, 255, 255, 230)));

    double maxTime = 0;

    signalDataX.clear();
    signalDataY.clear();
    signalDataZ.clear();

    // First, determine the global min and max for all data combined
    double globalMin = std::numeric_limits<double>::max();
    double globalMax = std::numeric_limits<double>::lowest();

    for (auto it = dataMap.begin(); it != dataMap.end(); ++it) {
        double localMin = *std::min_element(it.value().begin(), it.value().end());
        double localMax = *std::max_element(it.value().begin(), it.value().end());
        if (localMin < globalMin) globalMin = localMin;
        if (localMax > globalMax) globalMax = localMax;
    }

    // Calculate the center of the global range
    double globalCenter = (globalMax + globalMin) / 2;

    for (auto it = dataMap.begin(); it != dataMap.end(); ++it) {
        QVector<double> x(it.value().size());
        for (int i = 0; i < it.value().size(); ++i) {
            x[i] = static_cast<double>(i) / samplingRate;
        }

        QString key = it.key();

        if (!colorMap.contains(configName + key)) {
            colorMap[configName + key] = generateRandomColor();
        }
        QColor baseColor = colorMap.value(configName + key);

        QCPGraph *graph = customPlot->addGraph();
        if (graph) {
            QPen pen(baseColor);
            pen.setWidth(penWidth);
            graph->setPen(pen);
            graph->setBrush(Qt::NoBrush);
            graph->setLineStyle(QCPGraph::lsLine);

            // Avoid "Audio Audio" label
            if (configName == key) {
                graph->setName(configName);
            } else {
                graph->setName(configName + " " + key);
            }

            // Store the original y-values for use in cursor display
            QVector<double> originalYValues = it.value();

            // Calculate the local center and the offset to align it with the global center
            double localMin = *std::min_element(it.value().begin(), it.value().end());
            double localMax = *std::max_element(it.value().begin(), it.value().end());
            double localCenter = (localMax + localMin) / 2;
            double offset = globalCenter - localCenter;

            signalOffsets[key] = offset;  // Store the offset for this signal

            // Apply the offset to the y-values for plotting, but store the original values for cursor display
            QVector<double> yOffsetValues = it.value();
            for (int i = 0; i < yOffsetValues.size(); ++i) {
                yOffsetValues[i] += offset;
            }
            graph->setData(x, yOffsetValues);

            // Store signal data for cursor display
            if (key == "X") {
                for (int i = 0; i < x.size(); ++i) {
                    signalDataX.append(qMakePair(x[i], originalYValues[i]));
                }
            } else if (key == "Y") {
                for (int i = 0; i < x.size(); ++i) {
                    signalDataY.append(qMakePair(x[i], originalYValues[i]));
                }
            } else if (key == "Z") {
                for (int i = 0; i < x.size(); ++i) {
                    signalDataZ.append(qMakePair(x[i], originalYValues[i]));
                }
            }
        }

        if (!x.isEmpty() && x.last() > maxTime) {
            maxTime = x.last();
        }
    }

    customPlot->xAxis->setRange(0, maxTime);
    xAxisMinLimit = 0;
    xAxisMaxLimit = maxTime;

    // Add padding to the Y-axis range
    double padding = (globalMax - globalMin) * 0.1; // 10% padding
    if (padding == 0) { // Handle case where globalMax == globalMin
        padding = 1; // Set a default padding value
    }
    customPlot->yAxis->setRange(globalMin - padding, globalMax + padding);

    // Configure top X-axis for audio
    if (configName == "Audio") {
        customPlot->xAxis2->setVisible(true);
        customPlot->xAxis2->setTickLabels(true);
        customPlot->xAxis2->setTicks(true); // Enable ticks
        customPlot->xAxis2->setLabel("Time (s)");
        customPlot->xAxis2->setRange(0, maxTime);
        customPlot->xAxis2->setBasePen(QPen(Qt::black)); // Ensure the axis line is visible
        customPlot->xAxis2->setTickPen(QPen(Qt::black)); // Ensure the ticks are visible
        customPlot->xAxis2->setSubTickPen(QPen(Qt::black)); // Ensure the sub ticks are visible
    }

    customPlot->replot();
}

// Function to set up the custom plot with default settings
void KinematicVisualizer::setupCustomPlot() {
    customPlot->clearPlottables();
    customPlot->xAxis->setTicks(false);
    customPlot->xAxis->setTickLabels(false);
    customPlot->xAxis->setBasePen(Qt::NoPen);
    customPlot->xAxis->setTickPen(Qt::NoPen);
    customPlot->xAxis->setSubTickPen(Qt::NoPen);

    customPlot->yAxis->setTicks(false);
    customPlot->yAxis->setTickLabels(false);

    customPlot->axisRect()->setMargins(QMargins(0, 0, 0, 0));
    customPlot->axisRect()->setMinimumMargins(QMargins(0, 0, 0, 0));

    customPlot->setProperty("isSpectrogram", false);

    // Ensure xAxis2 (top axis) is configured properly
    customPlot->xAxis2->setVisible(false);
    customPlot->xAxis2->setTickLabels(false);
    customPlot->xAxis2->setTicks(false); // Initially disable ticks
    customPlot->xAxis2->setBasePen(Qt::NoPen);
    customPlot->xAxis2->setTickPen(Qt::NoPen);
    customPlot->xAxis2->setSubTickPen(Qt::NoPen);
}

// Function to get the selection range
QCPRange KinematicVisualizer::getSelectionRange() const {
    if (selectionRect) {
        double lower = selectionRect->topLeft->coords().x();
        double upper = selectionRect->bottomRight->coords().x();
        return QCPRange(lower, upper);
    }
    return QCPRange(0, 0);  // Return an invalid range if no selection
}

// Mouse press event handler to clear any existing selection rectangle and start a new one
void KinematicVisualizer::onAnyMousePress(QMouseEvent *event) {
    clearSelectionRect();  // Clear existing selection on any mouse press
    if (customPlot->viewport().contains(event->pos())) {
        selecting = true;
        selectionRect = new QCPItemRect(customPlot);
        selectionRect->setPen(QPen(Qt::NoPen));
        selectionRect->setBrush(QBrush(QColor(255, 0, 0, 50)));
        selectionRect->topLeft->setCoords(customPlot->xAxis->pixelToCoord(cursorPos.x()), customPlot->yAxis->range().upper);
        selectionRect->bottomRight->setCoords(customPlot->xAxis->pixelToCoord(cursorPos.x()), customPlot->yAxis->range().lower);
        customPlot->replot();
    }
}

// Mouse release event handler to finalize the selection rectangle
void KinematicVisualizer::onMouseRelease() {
    selecting = false;
    if (selectionRect) {
        selectionRect->bottomRight->setCoords(customPlot->xAxis->pixelToCoord(cursorPos.x()), customPlot->yAxis->range().lower);
        customPlot->replot();
    }
}

// Mouse drag event handler to update the selection rectangle
void KinematicVisualizer::onMouseDrag() {
    if (selecting && selectionRect) {
        selectionRect->bottomRight->setCoords(customPlot->xAxis->pixelToCoord(cursorPos.x()), customPlot->yAxis->range().lower);
        customPlot->replot();
    }
}

// Function to clear any existing selection rectangle
void KinematicVisualizer::clearSelectionRect() {
    if (selectionRect) {
        if (selectionRect->parentPlot()) {
            selectionRect->parentPlot()->removeItem(selectionRect);
        }
        selectionRect = nullptr;
        for (QCustomPlot *plot : customPlots) {
            plot->replot();
        }
    }
}

// Slot to synchronize the x-axis range of all plots
void KinematicVisualizer::synchronizePlots(const QCPRange &newRange) {
    for (QCustomPlot *plot : customPlots) {
        if (plot != customPlot) {
            plot->blockSignals(true); // Temporarily block signals to prevent infinite loop
            plot->xAxis->setRange(newRange);
            plot->replot();
            plot->blockSignals(false); // Re-enable signals
        }
    }
}

// Function to set the zoom limits for the x-axis
void KinematicVisualizer::setZoomLimits(double minLimit, double maxLimit) {
    xAxisMinLimit = minLimit;
    xAxisMaxLimit = maxLimit;
}

// Function to check if the current zoom level is at the limit
bool KinematicVisualizer::isAtZoomOutLimit() {
    QCPRange currentRange = customPlot->xAxis->range();
    return (currentRange.lower <= xAxisMinLimit && currentRange.upper >= xAxisMaxLimit);
}

// Function to visualize spectrogram data
void KinematicVisualizer::visualizeSpectrogram(const QVector<QVector<double>> &spectrogramData, const QString &configName, double duration) {
    setupCustomPlot();

    customPlot->setProperty("isSpectrogram", true);
    customPlot->setFixedHeight(150);

    int nx = spectrogramData.size();
    int ny = spectrogramData[0].size();

    QCPColorMap *colorMap = new QCPColorMap(customPlot->xAxis, customPlot->yAxis);
    colorMap->data()->setSize(nx, ny);
    colorMap->data()->setRange(QCPRange(0, duration), QCPRange(0, 5000));

    for (int x = 0; x < nx; ++x) {
        for (int y = 0; y < ny; ++y) {
            colorMap->data()->setCell(x, y, spectrogramData[x][y]);
        }
    }

    QCPColorGradient grayGradient;
    grayGradient.clearColorStops();
    grayGradient.setColorInterpolation(QCPColorGradient::ciRGB);
    grayGradient.setColorStopAt(0.0, Qt::white);
    grayGradient.setColorStopAt(1.0, Qt::black);
    colorMap->setGradient(grayGradient);

    double dataMin = std::numeric_limits<double>::max();
    double dataMax = std::numeric_limits<double>::lowest();
    for (const auto& row : spectrogramData) {
        for (const auto& value : row) {
            if (value < dataMin) dataMin = value;
            if (value > dataMax) dataMax = value;
        }
    }
    colorMap->setDataRange(QCPRange(dataMin, dataMax / 2));

    customPlot->rescaleAxes();

    colorMap->setName("");

    customPlot->replot();
}

// Function to set the tracked parameter (X, Y, or Z)
void KinematicVisualizer::setTrackedParameter(const QString &parameter) {
    trackedParameter = parameter;
}

// Function to set the signal data
void KinematicVisualizer::setSignalData(const QMap<QString, QVector<double>> &dataMap) {
    signalDataX.clear();
    signalDataY.clear();
    signalDataZ.clear();

    if (dataMap.contains("X")) {
        const QVector<double> &xData = dataMap["X"];
        for (int i = 0; i < xData.size(); ++i) {
            signalDataX.append(qMakePair(static_cast<double>(i), xData[i]));
        }
    }
    if (dataMap.contains("Y")) {
        const QVector<double> &yData = dataMap["Y"];
        for (int i = 0; i < yData.size(); ++i) {
            signalDataY.append(qMakePair(static_cast<double>(i), yData[i]));
        }
    }
    if (dataMap.contains("Z")) {
        const QVector<double> &zData = dataMap["Z"];
        for (int i = 0; i < zData.size(); ++i) {
            signalDataZ.append(qMakePair(static_cast<double>(i), zData[i]));
        }
    }
}

// Slot to zoom into the selected range
void KinematicVisualizer::zoomToSelection() {
    if (selectionRect) {
        double xMin = selectionRect->topLeft->coords().x();
        double xMax = selectionRect->bottomRight->coords().x();
        customPlot->xAxis->setRange(xMin, xMax);
        customPlot->replot();
        clearSelectionRect();  // Clear selection rectangle after zooming
    }
}

