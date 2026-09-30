#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>
#include <QtGlobal>
#include <QString>

#include <vector>

#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>

#include <QWheelEvent>
#include <QMouseEvent>

#include "dataparser.h"
#include "datacache.h"
#include "recorddatasource.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE


class InteractiveChartView : public QChartView
{
public:

    explicit InteractiveChartView(
        QChart *chart,
        QValueAxis *axisX,
        QValueAxis *axisY,
        QWidget *parent = nullptr)
        : QChartView(chart, parent)
        , m_axisX(axisX)
        , m_axisY(axisY)
    {
        setMouseTracking(true);
        setRubberBand(QChartView::RectangleRubberBand);
    }

    bool manualYRangeActive() const
    {
        return m_manualYRange;
    }

    void setAutomaticYRange()
    {
        m_manualYRange = false;
    }

    void configureXNavigation(
        double limitMin,
        double limitMax,
        double resetMin,
        double resetMax,
        double minRange,
        double maxRange)
    {
        m_limitMin = limitMin;
        m_limitMax = limitMax;
        m_resetMin = resetMin;
        m_resetMax = resetMax;
        m_minRange = minRange;
        m_maxRange = maxRange;

        m_manualYRange = false;

        if (m_axisX) {
            m_axisX->setRange(
                m_resetMin,
                m_resetMax);
        }
    }

protected:

    void wheelEvent(QWheelEvent *event) override
    {
        if (!m_axisX) {
            QChartView::wheelEvent(event);
            return;
        }

        const double oldMin = m_axisX->min();
        const double oldMax = m_axisX->max();
        const double oldRange = oldMax - oldMin;

        if (oldRange <= 0.0) {
            event->accept();
            return;
        }

        const double factor =
            (event->angleDelta().y() > 0)
                ? 0.70
                : 1.0 / 0.70;

        double newRange = oldRange * factor;

        if (newRange < m_minRange) {
            newRange = m_minRange;
        }

        if (newRange > m_maxRange) {
            newRange = m_maxRange;
        }

        const double availableRange =
            m_limitMax - m_limitMin;

        if (newRange > availableRange) {
            newRange = availableRange;
        }

        QAbstractSeries *referenceSeries =
            nullptr;

        if (!chart()->series().isEmpty()) {
            referenceSeries =
                chart()->series().first();
        }

        const QPointF scenePosition =
            mapToScene(
                event->position().toPoint());

        const QPointF chartPosition =
            chart()->mapFromScene(
                scenePosition);

        const QPointF valuePosition =
            chart()->mapToValue(
                chartPosition,
                referenceSeries);

        double center = valuePosition.x();

        if (center < oldMin || center > oldMax) {
            center = (oldMin + oldMax) * 0.5;
        }

        const double mouseRatio =
            (center - oldMin) / oldRange;

        double newMin =
            center - mouseRatio * newRange;

        double newMax =
            newMin + newRange;

        clampXRange(
            newMin,
            newMax);

        m_axisX->setRange(
            newMin,
            newMax);

        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const QPointF scenePos =
            mapToScene(
                event->position().toPoint());

        const bool insidePlot =
            chart()->plotArea().contains(
                chart()->mapFromScene(
                    scenePos));

        if (event->button() == Qt::RightButton &&
            insidePlot)
        {
            m_panning = true;
            m_lastMousePosition = event->position();
            setCursor(Qt::ClosedHandCursor);

            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton &&
            insidePlot)
        {
            m_leftSelecting = true;
            m_leftPressPosition = event->position();
        }

        QChartView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_panning && m_axisX && m_axisY) {

            const QPointF currentPosition =
                event->position();

            const QRectF plotArea =
                chart()->plotArea();

            if (plotArea.width() > 0.0 &&
                plotArea.height() > 0.0)
            {
                const double pixelDeltaX =
                    currentPosition.x()
                    - m_lastMousePosition.x();

                const double pixelDeltaY =
                    currentPosition.y()
                    - m_lastMousePosition.y();

                const double xRange =
                    m_axisX->max()
                    - m_axisX->min();

                const double xValueDelta =
                    pixelDeltaX
                    / plotArea.width()
                    * xRange;

                double newXMin =
                    m_axisX->min()
                    - xValueDelta;

                double newXMax =
                    m_axisX->max()
                    - xValueDelta;

                clampXRange(
                    newXMin,
                    newXMax);

                m_axisX->setRange(
                    newXMin,
                    newXMax);

                const double yRange =
                    m_axisY->max()
                    - m_axisY->min();

                const double yValueDelta =
                    pixelDeltaY
                    / plotArea.height()
                    * yRange;

                m_axisY->setRange(
                    m_axisY->min() + yValueDelta,
                    m_axisY->max() + yValueDelta);

                m_manualYRange = true;
            }

            m_lastMousePosition =
                currentPosition;

            event->accept();
            return;
        }

        QChartView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton &&
            m_panning)
        {
            m_panning = false;
            unsetCursor();

            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton &&
            m_leftSelecting)
        {
            const QPointF releasePosition =
                event->position();

            const double dx =
                releasePosition.x()
                - m_leftPressPosition.x();

            const double dy =
                releasePosition.y()
                - m_leftPressPosition.y();

            QChartView::mouseReleaseEvent(event);

            if ((dx * dx + dy * dy) >= 25.0) {
                m_manualYRange = true;
            }

            m_leftSelecting = false;
            return;
        }

        QChartView::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(
        QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton &&
            m_axisX)
        {
            m_axisX->setRange(
                m_resetMin,
                m_resetMax);

            m_manualYRange = false;

            event->accept();
            return;
        }

        QChartView::mouseDoubleClickEvent(event);
    }

private:

    void clampXRange(
        double &minimum,
        double &maximum) const
    {
        const double range =
            maximum - minimum;

        if (minimum < m_limitMin) {
            minimum = m_limitMin;
            maximum = minimum + range;
        }

        if (maximum > m_limitMax) {
            maximum = m_limitMax;
            minimum = maximum - range;
        }

        if (minimum < m_limitMin) {
            minimum = m_limitMin;
        }

        if (maximum > m_limitMax) {
            maximum = m_limitMax;
        }
    }

    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    bool m_panning = false;
    bool m_leftSelecting = false;
    bool m_manualYRange = false;

    QPointF m_lastMousePosition;
    QPointF m_leftPressPosition;

    double m_limitMin = -5.0;
    double m_limitMax = 0.0;
    double m_resetMin = -5.0;
    double m_resetMax = 0.0;
    double m_minRange = 0.001;
    double m_maxRange = 5.0;
};


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

private:

    Ui::MainWindow *ui;

    void connectDataStream();
    void disconnectDataStream();
    void enterLiveMode();

    void openRecordForDisplay(
        const QString &localFile);

    void loadVisibleRecordRange();

    struct RecordDisplaySample
    {
        qint64 frameIndex = 0;
        DataFrame frame;
    };

    void displayRecordSamples(
        const std::vector<RecordDisplaySample> &samples);

    static constexpr double RECORD_SAMPLE_RATE = 500000.0;
    static constexpr qint64 RECORD_TARGET_POINTS = 6000;
    static constexpr qint64 RECORD_DIRECT_READ_LIMIT = 20000;

    QTcpSocket *controlSocket;
    QTcpSocket *dataSocket;

    DataParser dataParser;
    DataCache dataCache;

    RecordDataSource recordSource;

    bool recordMode = false;
    qint64 recordTotalFrames = 0;
    double recordDurationSeconds = 0.0;

    qint64 loadedRecordFirstFrame = -1;
    qint64 loadedRecordLastFrame = -1;
    qint64 loadedRecordStride = -1;

    QTimer *displayTimer;
    QTimer *statisticsTimer;
    QTimer *recordLoadTimer;

    quint64 receivedBytes = 0;
    quint64 receivedFrames = 0;
    quint64 displayUpdates = 0;

    QChart *currentChart = nullptr;

    InteractiveChartView *currentChartView = nullptr;

    QLineSeries *i1Series = nullptr;
    QLineSeries *i2Series = nullptr;
    QLineSeries *i3Series = nullptr;

    QValueAxis *currentAxisX = nullptr;
    QValueAxis *currentAxisY = nullptr;

    QChart *rpmChart = nullptr;
    QChartView *rpmChartView = nullptr;

    QLineSeries *rpmSeries = nullptr;

    QValueAxis *rpmAxisX = nullptr;
    QValueAxis *rpmAxisY = nullptr;

    quint64 plottedSampleCount = 0;

    bool displayPaused = false;
};

#endif // MAINWINDOW_H