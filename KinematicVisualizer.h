//
// Created by Mikhail Vorotnikov on 8/26/24.
//

#ifndef KINEMATICVISUALIZER_H
#define KINEMATICVISUALIZER_H

#include <QWidget>
#include "qcustomplot.h"
#include "label.h"

// KinematicVisualizer class for visualizing kinematic signals and spectrograms
class KinematicVisualizer : public QWidget {
    Q_OBJECT

public:
    explicit KinematicVisualizer(QWidget *parent = nullptr);

    // Event filter for handling custom events like mouse movements and widget events
    bool eventFilter(QObject *object, QEvent *event) override;

    // Getter for the custom plot
    QCustomPlot* getCustomPlot() const;

    // Visualization methods
    void visualizeSignal(const QMap<QString, QVector<double>> &dataMap, const QString &configName, int penWidth, int samplingRate);
    void visualizeSpectrogram(const QVector<QVector<double>> &spectrogramData, const QString &configName, double duration);

    // Destructor
    ~KinematicVisualizer();

    // Setters
    void setTrackedParameter(const QString &parameter);
    void setSignalData(const QMap<QString, QVector<double>> &dataMap);

    // Method to clear the selection rectangle
    void clearSelectionRect();

    // Getters for X-axis limits
    double getXAxisMinLimit() const;
    double getXAxisMaxLimit() const;

    // Getter for selection range
    QCPRange getSelectionRange() const;

    // Getter for the associated label object
    Label* getLabel() const;

public slots:
            // Slot to zoom into the selected range
            void zoomToSelection();

private slots:
            // Overridden event handlers
            void mouseMoveEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

    // Slot to synchronize the plots based on the X-axis range
    void synchronizePlots(const QCPRange &newRange);

    // Mouse event handlers
    void onMouseRelease();
    void onMouseDrag();
    void onAnyMousePress(QMouseEvent *event);

private:
    // Private members for graphical items
    QCPItemRect *coordFrame;       // Coordinate frame for the cursor
    QCustomPlot *customPlot;       // Custom plot for visualizing signals
    QCPItemLine *hLine;            // Horizontal line for the cursor
    QCPItemLine *vLine;            // Vertical line for the cursor
    QCPItemText *coordText;        // Coordinate text for the cursor

    // Methods for setting zoom limits
    void setZoomLimits(double minLimit, double maxLimit);
    bool isAtZoomOutLimit();

    // Method for plotting signal data
    void plotSignalData(const QVector<double> &data, const QString &configName);

    // Method to set up the custom plot with default settings
    void setupCustomPlot();

    // Static members for color mapping and cursor synchronization
    static QMap<QString, QColor> colorMap;              // Color map for different signals
    static QColor generateRandomColor();                // Function to generate random colors

    // Cursor and selection management
    QPoint cursorPos;                                   // Current cursor position
    bool showCursor;                                    // Flag to indicate if the cursor should be shown

    // Axis limits
    double xAxisMinLimit;
    double xAxisMaxLimit;
    double yAxisMinLimit;
    double yAxisMaxLimit;

        // Static methods for synchronizing Y-axes across multiple plots
    static void synchronizeYAxes();

    // Static members for managing multiple custom plots and their vertical lines
    static QList<QCustomPlot*> customPlots;
    static QMap<QCustomPlot*, QCPItemLine*> vLinesMap;
    static QCustomPlot* lastPlotWithLine;

    // Methods for setting up and updating cursor items
    void setupCursorItems(QCustomPlot *plot);
    void updateCursorItems(QCustomPlot *plot);
    void hideHorizontalCursor();
    void hideAllVerticalLines();
    void updateVerticalLineInAllPlots(double x);
    void hideVerticalLine();
    void updateVerticalLine(double x, QCustomPlot *currentPlot);

    // List to store vertical lines
    QList<QCPItemLine*> vLines;

    // Method to hide vertical lines in all plots
    void hideVerticalLineInAllPlots();

    // Current plot being interacted with
    QCustomPlot *currentPlot;

    // Method to retrieve Y-value from signal data based on the X-value
    double getYValueFromSignal(double x);

    // Data for signals X, Y, and Z
    QVector<QPair<double, double>> signalDataX;
    QVector<QPair<double, double>> signalDataY;
    QVector<QPair<double, double>> signalDataZ;

    // Tracked parameter (e.g., "X", "Y", or "Z")
    QString trackedParameter;

    // Horizontal scroll bar (if needed)
    QScrollBar *horizontalScrollBar;

    // Static selection rectangle for zooming
    static QCPItemRect *selectionRect;

    // Flag to indicate if selection is in progress
    bool selecting;

    // Map to store offsets for different signals
    QMap<QString, double> signalOffsets;

    // Label object associated with the visualizer
    Label *label;
};

#endif // KINEMATICVISUALIZER_H

