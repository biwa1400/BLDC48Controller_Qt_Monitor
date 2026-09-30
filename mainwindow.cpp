#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "recordfiledialog.h"
#include "recorddatasource.h"

#include <QDebug>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QLegendMarker>
#include <QAbstractSeries>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QTabBar>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLayout>

#include <algorithm>
#include <limits>


MainWindow::MainWindow(
    QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , controlSocket(
          new QTcpSocket(this))
    , dataSocket(
          new QTcpSocket(this))
    , displayTimer(
          new QTimer(this))
    , statisticsTimer(
          new QTimer(this))
    , recordLoadTimer(
          new QTimer(this))
{
    ui->setupUi(this);


    // =========================================================
    // Single-page UI + Record reader button
    // =========================================================

    // Remove the old Record / Analysis page at runtime.
    // The .ui file remains unchanged.
    if (ui->mainTabWidget->count() > 1) {

        QWidget *recordPage =
            ui->mainTabWidget->widget(1);

        ui->mainTabWidget->removeTab(1);

        recordPage->deleteLater();
    }


    // Only one page remains, so hide the tab bar.
    ui->mainTabWidget->tabBar()->hide();


    auto *readRecordButton =
        new QPushButton(
            "Read Record",
            this);


    readRecordButton->setMinimumSize(
        110,
        30);


    readRecordButton->setStyleSheet(
        "QPushButton {"
        "font-weight: bold;"
        "padding: 4px 12px;"
        "}");


    const int pauseIndex =
        ui->statusLayout->indexOf(
            ui->pauseButton);


    ui->statusLayout->insertWidget(
        pauseIndex,
        readRecordButton);


    // =========================================================
    // Bottom motor control bar
    // =========================================================

    auto *motorControlBar =
        new QWidget(this);


    auto *motorControlLayout =
        new QHBoxLayout(
            motorControlBar);


    motorControlLayout->setContentsMargins(
        0,
        6,
        0,
        0);


    motorControlLayout->setSpacing(12);


    auto *runButton =
        new QPushButton(
            "RUN",
            motorControlBar);


    auto *stopButton =
        new QPushButton(
            "STOP",
            motorControlBar);


    auto *recordButton =
        new QPushButton(
            "RECORD",
            motorControlBar);


    const QSize motorButtonSize(
        130,
        38);


    runButton->setMinimumSize(
        motorButtonSize);

    stopButton->setMinimumSize(
        motorButtonSize);

    recordButton->setMinimumSize(
        motorButtonSize);


    runButton->setStyleSheet(
        "QPushButton {"
        "font-weight: bold;"
        "font-size: 14px;"
        "padding: 6px 18px;"
        "color: white;"
        "background-color: #2E7D32;"
        "border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "background-color: #388E3C;"
        "}"
        "QPushButton:pressed {"
        "background-color: #1B5E20;"
        "}");


    stopButton->setStyleSheet(
        "QPushButton {"
        "font-weight: bold;"
        "font-size: 14px;"
        "padding: 6px 18px;"
        "color: white;"
        "background-color: #C62828;"
        "border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "background-color: #D32F2F;"
        "}"
        "QPushButton:pressed {"
        "background-color: #8E0000;"
        "}");


    recordButton->setStyleSheet(
        "QPushButton {"
        "font-weight: bold;"
        "font-size: 14px;"
        "padding: 6px 18px;"
        "color: white;"
        "background-color: #1565C0;"
        "border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "background-color: #1976D2;"
        "}"
        "QPushButton:pressed {"
        "background-color: #0D47A1;"
        "}");


    motorControlLayout->addStretch();
    motorControlLayout->addWidget(runButton);
    motorControlLayout->addWidget(stopButton);
    motorControlLayout->addWidget(recordButton);
    motorControlLayout->addStretch();


    if (centralWidget() &&
        centralWidget()->layout())
    {
        centralWidget()->layout()->addWidget(
            motorControlBar);
    }


    // =========================================================
    // Manual control commands on controlSocket :8889
    // =========================================================

    auto sendControlCommand =
        [this](
            char command,
            const char *name)
    {
        if (controlSocket->state() !=
            QAbstractSocket::ConnectedState)
        {
            qDebug()
            << "CONTROL:"
            << name
            << "not sent - socket not connected";

            QMessageBox::warning(
                this,
                "Control Not Connected",
                "The control socket is not connected.");

            return;
        }

        controlSocket->write(
            &command,
            1);

        controlSocket->flush();

        qDebug()
            << "CONTROL: Sent"
            << name
            << QString("0x%1")
                   .arg(
                       static_cast<int>(
                           static_cast<unsigned char>(command)),
                       2,
                       16,
                       QLatin1Char('0'))
                   .toUpper();
    };


    connect(
        runButton,
        &QPushButton::clicked,
        this,
        [sendControlCommand]()
        {
            sendControlCommand(
                0x01,
                "RUN");
        });


    connect(
        stopButton,
        &QPushButton::clicked,
        this,
        [sendControlCommand]()
        {
            sendControlCommand(
                0x02,
                "STOP");
        });


    connect(
        recordButton,
        &QPushButton::clicked,
        this,
        [sendControlCommand]()
        {
            sendControlCommand(
                0x03,
                "RECORD");
        });


    // =========================================================
    // RAW ADC / Custom FIR / Peak Value chart
    // =========================================================

    i1Series =
        new QLineSeries(this);

    i2Series =
        new QLineSeries(this);

    i3Series =
        new QLineSeries(this);


    i1Series->setName(
        "RAW ADC0");

    i2Series->setName(
        "CUSTOM FIR");

    i3Series->setName(
        "PEAK VALUE");


    // Fixed colors

    i1Series->setPen(
        QPen(
            QColor("#2196F3"),
            2.0)); // Blue

    i2Series->setPen(
        QPen(
            QColor("#4CAF50"),
            2.0)); // Green

    i3Series->setPen(
        QPen(
            QColor("#FF9800"),
            2.0)); // Orange


    currentChart =
        new QChart();


    currentChart->addSeries(
        i1Series);

    currentChart->addSeries(
        i2Series);

    currentChart->addSeries(
        i3Series);


    currentChart->setTitle("");


    currentChart
        ->legend()
        ->setVisible(true);

    currentChart
        ->legend()
        ->setAlignment(
            Qt::AlignTop);


    // =========================================================
    // Click legend item to show/hide series
    // =========================================================

    for (QLegendMarker *marker :
         currentChart
             ->legend()
             ->markers())
    {
        connect(
            marker,
            &QLegendMarker::clicked,
            this,
            [marker]()
            {
                QAbstractSeries *series =
                    marker->series();

                if (!series) {
                    return;
                }

                const bool newVisible =
                    !series->isVisible();

                series->setVisible(
                    newVisible);

                // Keep marker visible even if
                // the corresponding curve is hidden.
                marker->setVisible(true);
            });
    }


    currentChart->setMargins(
        QMargins(
            5,
            5,
            5,
            5));


    // =========================================================
    // Current chart axes
    // =========================================================

    currentAxisX =
        new QValueAxis();

    currentAxisY =
        new QValueAxis();


    currentAxisX->setTitleText(
        "Time (s)");

    currentAxisY->setTitleText(
        "Raw value");


    currentAxisX->setRange(
        -5.0,
        0.0);

    currentAxisX->setTickCount(6);

    currentAxisX->setLabelFormat(
        "%.3f");


    currentAxisY->setRange(
        -1000.0,
        1000.0);

    currentAxisY->setTickCount(7);

    currentAxisY->setLabelFormat(
        "%.0f");


    currentChart->addAxis(
        currentAxisX,
        Qt::AlignBottom);

    currentChart->addAxis(
        currentAxisY,
        Qt::AlignLeft);


    i1Series->attachAxis(
        currentAxisX);

    i1Series->attachAxis(
        currentAxisY);


    i2Series->attachAxis(
        currentAxisX);

    i2Series->attachAxis(
        currentAxisY);


    i3Series->attachAxis(
        currentAxisX);

    i3Series->attachAxis(
        currentAxisY);


    // =========================================================
    // Interactive current chart view
    // =========================================================

    currentChartView =
        new InteractiveChartView(
            currentChart,
            currentAxisX,
            currentAxisY);


    currentChartView->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding);


    currentChartView->setRenderHint(
        QPainter::Antialiasing,
        false);


    ui->currentChartLayout->addWidget(
        currentChartView);


    // Default navigation = 5-second live view.
    currentChartView->configureXNavigation(
        -5.0,
        0.0,
        -5.0,
        0.0,
        0.001,
        5.0);


    // =========================================================
    // Lazy Record loading
    //
    // Axis changes are debounced so right-button panning and
    // wheel zoom do not cause a disk read for every mouse event.
    // =========================================================

    recordLoadTimer->setSingleShot(true);
    recordLoadTimer->setInterval(60);


    connect(
        recordLoadTimer,
        &QTimer::timeout,
        this,
        &MainWindow::loadVisibleRecordRange);


    connect(
        currentAxisX,
        &QValueAxis::rangeChanged,
        this,
        [this](qreal, qreal)
        {
            if (recordMode) {
                recordLoadTimer->start();
            }
        });


    // =========================================================
    // LM10 RPM chart
    // =========================================================

    rpmSeries =
        new QLineSeries(this);


    rpmSeries->setName(
        "LM10 RPM");


    rpmSeries->setPen(
        QPen(
            QColor("#9C27B0"),
            2.0));


    rpmChart =
        new QChart();


    rpmChart->addSeries(
        rpmSeries);


    rpmChart->setTitle("");


    rpmChart
        ->legend()
        ->setVisible(false);


    rpmChart->setMargins(
        QMargins(
            5,
            5,
            5,
            5));


    // =========================================================
    // RPM axes
    // =========================================================

    rpmAxisX =
        new QValueAxis();

    rpmAxisY =
        new QValueAxis();


    rpmAxisX->setTitleText(
        "Time (s)");

    rpmAxisY->setTitleText(
        "RPM");


    rpmAxisX->setRange(
        -5.0,
        0.0);

    rpmAxisX->setTickCount(6);

    rpmAxisX->setLabelFormat(
        "%.1f");


    rpmAxisY->setRange(
        -100.0,
        100.0);

    rpmAxisY->setTickCount(6);

    rpmAxisY->setLabelFormat(
        "%.0f");


    rpmChart->addAxis(
        rpmAxisX,
        Qt::AlignBottom);

    rpmChart->addAxis(
        rpmAxisY,
        Qt::AlignLeft);


    rpmSeries->attachAxis(
        rpmAxisX);

    rpmSeries->attachAxis(
        rpmAxisY);


    // =========================================================
    // RPM chart view
    // =========================================================

    rpmChartView =
        new QChartView(
            rpmChart);


    rpmChartView->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding);


    rpmChartView->setRenderHint(
        QPainter::Antialiasing,
        false);


    ui->rpmChartLayout->addWidget(
        rpmChartView);


    // =========================================================
    // Data socket
    // =========================================================

    connect(
        dataSocket,
        &QTcpSocket::connected,
        this,
        [this]()
        {
            qDebug()
            << "DATA: Connected";


            ui->connectionLabel->setText(
                "● Connected");


            ui->connectionLabel->setStyleSheet(
                "color: #18A538;"
                "font-weight: bold;");
        });


    connect(
        dataSocket,
        &QTcpSocket::disconnected,
        this,
        [this]()
        {
            qDebug()
            << "DATA: Disconnected";


            ui->connectionLabel->setText(
                "● Disconnected");


            ui->connectionLabel->setStyleSheet(
                "color: #D32F2F;"
                "font-weight: bold;");
        });


    connect(
        dataSocket,
        &QTcpSocket::readyRead,
        this,
        [this]()
        {
            const QByteArray data =
                dataSocket->readAll();


            receivedBytes +=
                static_cast<quint64>(
                    data.size());


            const std::vector<DataFrame>
                frames =
                dataParser.appendData(
                    data);


            receivedFrames +=
                static_cast<quint64>(
                    frames.size());


            for (const DataFrame &frame :
                 frames)
            {
                dataCache.push(frame);
            }
        });


    connect(
        dataSocket,
        &QTcpSocket::errorOccurred,
        this,
        [this](
            QAbstractSocket::SocketError)
        {
            qDebug()
            << "DATA ERROR:"
            << dataSocket->errorString();


            ui->connectionLabel->setText(
                "● Data Error");


            ui->connectionLabel->setStyleSheet(
                "color: #D32F2F;"
                "font-weight: bold;");
        });


    // =========================================================
    // Real-time data Connect switch
    // =========================================================

    connect(
        ui->connectSwitch,
        &QCheckBox::toggled,
        this,
        [this](bool checked)
        {
            // This switch controls ONLY the real-time data socket.
            // It does NOT control the motor.

            if (checked) {

                enterLiveMode();


                if (ui->mainTabWidget
                        ->currentIndex() == 0)
                {
                    connectDataStream();
                }
            }
            else {

                disconnectDataStream();
            }
        });


    // =========================================================
    // Tab switching
    // =========================================================

    connect(
        ui->mainTabWidget,
        &QTabWidget::currentChanged,
        this,
        [this](int index)
        {
            if (index == 0) {

                qDebug()
                << "TAB: Real-time Monitor";


                if (ui->connectSwitch
                        ->isChecked())
                {
                    connectDataStream();
                }
            }
            else {

                qDebug()
                << "TAB: Record / Analysis";


                // Disconnect ONLY the real-time data socket.
                //
                // Control socket remains connected.
                // No motor command is sent.

                disconnectDataStream();
            }
        });


    // =========================================================
    // Read Record
    // =========================================================

    connect(
        readRecordButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            // Open the SSH browser only after dataSocket :8888
            // is confirmed disconnected.

            auto openRecordDialog =
                [this]()
            {
                RecordFileDialog dialog(
                    this);


                if (dialog.exec() ==
                    QDialog::Accepted)
                {
                    const QString remoteFile =
                        dialog.selectedRemotePath();


                    const QString localFile =
                        dialog.downloadedLocalPath();


                    qDebug()
                        << "RECORD: Remote file"
                        << remoteFile;


                    qDebug()
                        << "RECORD: Local file"
                        << localFile;


                    openRecordForDisplay(
                        localFile);
                }
            };


            // =================================================
            // SSH/SCP must not share USB/RNDIS bandwidth with
            // the real-time TCP data stream.
            //
            // Disconnect ONLY dataSocket :8888.
            // controlSocket :8889 stays connected.
            // NO STOP 0x02 command is sent.
            // =================================================

            if (dataSocket->state() !=
                QAbstractSocket::UnconnectedState)
            {
                connect(
                    dataSocket,
                    &QTcpSocket::disconnected,
                    this,
                    openRecordDialog,
                    Qt::SingleShotConnection);


                ui->connectSwitch->setChecked(
                    false);


                disconnectDataStream();
            }
            else
            {
                ui->connectSwitch->setChecked(
                    false);


                openRecordDialog();
            }
        });


    // =========================================================
    // Control socket
    // =========================================================

    connect(
        controlSocket,
        &QTcpSocket::connected,
        this,
        [this]()
        {
            qDebug()
            << "CONTROL: Connected";


            // Preserve original motor START behavior.

            const char startCommand =
                0x01;


            controlSocket->write(
                &startCommand,
                1);


            qDebug()
                << "CONTROL: Sent START 0x01";
        });


    connect(
        controlSocket,
        &QTcpSocket::readyRead,
        this,
        [this]()
        {
            const QByteArray response =
                controlSocket->readAll();


            qDebug()
                << "CONTROL: Received"
                << response.size()
                << "byte(s):"
                << response.toHex(' ');
        });


    connect(
        controlSocket,
        &QTcpSocket::errorOccurred,
        this,
        [this](
            QAbstractSocket::SocketError)
        {
            qDebug()
            << "CONTROL ERROR:"
            << controlSocket->errorString();
        });


    // =========================================================
    // Pause / Resume
    // =========================================================

    connect(
        ui->pauseButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            displayPaused =
                !displayPaused;


            if (displayPaused) {

                ui->pauseButton->setText(
                    "Resume");


                qDebug()
                    << "DISPLAY: Paused";
            }
            else {

                ui->pauseButton->setText(
                    "Pause");


                qDebug()
                    << "DISPLAY: Resumed";
            }
        });


    // =========================================================
    // Display timer
    // =========================================================

    // Target display rate = 50 FPS.

    displayTimer->setInterval(
        20);


    connect(
        displayTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            // Pause freezes only chart rendering.
            // TCP reception and cache continue.

            if (displayPaused) {
                return;
            }


            ++displayUpdates;


            if (dataCache.empty()) {
                return;
            }


            constexpr double DISPLAY_SECONDS =
                5.0;


            // Temporary PC display time base.
            //
            // This is ONLY for the real-time USB/TCP view.
            // It is NOT used by Record mode.

            constexpr double SAMPLE_RATE_ESTIMATE =
                1000.0;


            const std::size_t wantedSamples =
                static_cast<std::size_t>(
                    DISPLAY_SECONDS *
                    SAMPLE_RATE_ESTIMATE);


            const std::size_t cacheSize =
                dataCache.size();


            const std::size_t sampleCount =
                std::min(
                    cacheSize,
                    wantedSamples);


            const std::size_t firstIndex =
                cacheSize -
                sampleCount;


            // =================================================
            // Point buffers
            // =================================================

            QList<QPointF> i1Points;
            QList<QPointF> i2Points;
            QList<QPointF> i3Points;
            QList<QPointF> rpmPoints;


            i1Points.reserve(
                static_cast<qsizetype>(
                    sampleCount));

            i2Points.reserve(
                static_cast<qsizetype>(
                    sampleCount));

            i3Points.reserve(
                static_cast<qsizetype>(
                    sampleCount));

            rpmPoints.reserve(
                static_cast<qsizetype>(
                    sampleCount));


            // =================================================
            // ADC / FIR Y range
            // =================================================

            double currentMinY =
                std::numeric_limits<double>::max();


            double currentMaxY =
                std::numeric_limits<double>::lowest();


            // =================================================
            // RPM Y range
            // =================================================

            double rpmMinY =
                std::numeric_limits<double>::max();


            double rpmMaxY =
                std::numeric_limits<double>::lowest();


            // =================================================
            // Read cache
            // =================================================

            for (std::size_t i = 0;
                 i < sampleCount;
                 ++i)
            {
                const DataFrame &frame =
                    dataCache.at(
                        firstIndex + i);


                // Newest sample = 0 seconds.

                const double x =
                    -static_cast<double>(
                        sampleCount - 1 - i)
                    / SAMPLE_RATE_ESTIMATE;


                // ---------------------------------------------
                // ADC / FIR signals
                //
                // Blue:
                // currents[0] = original RAW ADC0
                //
                // Green:
                // currents[1] = custom FIR output for ADC0
                //
                // Orange:
                // currents[2] = ADC0 peak value
                // ---------------------------------------------

                const double y1 =
                    static_cast<double>(
                        frame.currents[0]);


                const double y2 =
                    static_cast<double>(
                        frame.currents[1]);


                const double y3 =
                    static_cast<double>(
                        frame.currents[2]);


                i1Points.append(
                    QPointF(
                        x,
                        y1));


                i2Points.append(
                    QPointF(
                        x,
                        y2));


                i3Points.append(
                    QPointF(
                        x,
                        y3));


                currentMinY =
                    std::min(
                        currentMinY,
                        y1);


                currentMinY =
                    std::min(
                        currentMinY,
                        y2);


                currentMinY =
                    std::min(
                        currentMinY,
                        y3);


                currentMaxY =
                    std::max(
                        currentMaxY,
                        y1);


                currentMaxY =
                    std::max(
                        currentMaxY,
                        y2);


                currentMaxY =
                    std::max(
                        currentMaxY,
                        y3);


                // ---------------------------------------------
                // LM10 RPM
                // ---------------------------------------------

                const double rpm =
                    static_cast<double>(
                        frame.rpm);


                rpmPoints.append(
                    QPointF(
                        x,
                        rpm));


                rpmMinY =
                    std::min(
                        rpmMinY,
                        rpm);


                rpmMaxY =
                    std::max(
                        rpmMaxY,
                        rpm);
            }


            // =================================================
            // Update series
            // =================================================

            i1Series->replace(
                i1Points);


            i2Series->replace(
                i2Points);


            i3Series->replace(
                i3Points);


            rpmSeries->replace(
                rpmPoints);


            // =================================================
            // RPM X axis
            //
            // ADC/FIR X axis is intentionally NOT reset here.
            // This allows manual zoom and pan to remain active.
            // =================================================

            rpmAxisX->setRange(
                -DISPLAY_SECONDS,
                0.0);


            // =================================================
            // Automatic ADC / FIR Y axis
            //
            // Once the user performs rectangle zoom or
            // right-button Y pan, automatic Y scaling stops.
            //
            // Left double-click restores automatic Y scaling.
            // =================================================

            if (!currentChartView
                     ->manualYRangeActive() &&
                currentMinY <= currentMaxY)
            {
                double range =
                    currentMaxY -
                    currentMinY;


                if (range < 10.0) {
                    range = 10.0;
                }


                const double margin =
                    range * 0.10;


                currentAxisY->setRange(
                    currentMinY - margin,
                    currentMaxY + margin);
            }


            // =================================================
            // Automatic RPM Y axis
            // =================================================

            if (rpmMinY <= rpmMaxY)
            {
                double range =
                    rpmMaxY -
                    rpmMinY;


                if (range < 100.0) {
                    range = 100.0;
                }


                const double margin =
                    range * 0.10;


                const double center =
                    (rpmMinY +
                     rpmMaxY)
                    / 2.0;


                rpmAxisY->setRange(
                    center -
                        range / 2.0 -
                        margin,

                    center +
                        range / 2.0 +
                        margin);
            }
        });


    displayTimer->start();


    // =========================================================
    // Statistics timer
    // =========================================================

    statisticsTimer->setInterval(
        1000);


    connect(
        statisticsTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            const double kiloBytesPerSecond =
                static_cast<double>(
                    receivedBytes)
                / 1000.0;


            ui->dataRateLabel->setText(
                QString(
                    "Data: %1 kB/s")
                    .arg(
                        kiloBytesPerSecond,
                        0,
                        'f',
                        1));


            ui->rxRateLabel->setText(
                QString(
                    "RX: %1 frames/s")
                    .arg(
                        receivedFrames));


            ui->displayFpsLabel->setText(
                QString(
                    "Display: %1 FPS")
                    .arg(
                        displayUpdates));


            // =================================================
            // Debug newest sample once per second
            // =================================================

            if (!dataCache.empty())
            {
                const DataFrame &frame =
                    dataCache.at(
                        dataCache.size() - 1);


                qDebug()
                    << "RAW ="
                    << frame.currents[0]

                    << "CUSTOM_FIR_CH0 ="
                    << frame.currents[1]

                    << "ADC_PEAK0 ="
                    << frame.currents[2];
            }


            receivedBytes =
                0;

            receivedFrames =
                0;

            displayUpdates =
                0;
        });


    statisticsTimer->start();


    // =========================================================
    // Connect control socket at startup
    //
    // Keep original behavior:
    //
    // controlSocket connects automatically.
    // Its connected callback sends START 0x01.
    //
    // Data socket is controlled separately by Connect.
    // =========================================================

    controlSocket->connectToHost(
        "192.168.7.2",
        8889);
}


// =============================================================
// Return to live display mode
// =============================================================

void MainWindow::enterLiveMode()
{
    recordLoadTimer->stop();

    recordMode = false;

    if (recordSource.isOpen()) {
        recordSource.close();
    }

    recordTotalFrames = 0;
    recordDurationSeconds = 0.0;

    loadedRecordFirstFrame = -1;
    loadedRecordLastFrame = -1;
    loadedRecordStride = -1;

    displayPaused = false;

    ui->pauseButton->setEnabled(true);
    ui->pauseButton->setText("Pause");

    currentAxisX->setTitleText("Time (s)");
    currentAxisX->setLabelFormat("%.3f");

    currentChartView->configureXNavigation(
        -5.0,
        0.0,
        -5.0,
        0.0,
        0.001,
        5.0);
}


// =============================================================
// Open a local Record file
// =============================================================

void MainWindow::openRecordForDisplay(
    const QString &localFile)
{
    recordLoadTimer->stop();

    recordMode = false;

    if (recordSource.isOpen()) {
        recordSource.close();
    }

    if (!recordSource.open(localFile))
    {
        QMessageBox::critical(
            this,
            "Record Read Error",
            QString(
                "Could not open the local record.\n\n"
                "%1\n\n"
                "%2")
                .arg(
                    localFile,
                    recordSource.lastError()));

        return;
    }

    recordTotalFrames =
        recordSource.totalFrames();

    if (recordTotalFrames <= 0)
    {
        QMessageBox::critical(
            this,
            "Record Read Error",
            "The Record file contains no complete frames.");

        recordSource.close();
        return;
    }

    // Last frame is at (N - 1) / Fs.
    recordDurationSeconds =
        static_cast<double>(
            recordTotalFrames - 1)
        / RECORD_SAMPLE_RATE;

    loadedRecordFirstFrame = -1;
    loadedRecordLastFrame = -1;
    loadedRecordStride = -1;

    // Freeze the 20 ms live renderer while browsing Record data.
    displayPaused = true;

    ui->pauseButton->setText("Record");
    ui->pauseButton->setEnabled(false);

    currentAxisX->setTitleText("Time (s)");
    currentAxisX->setLabelFormat("%.6f");

    const double minimumRecordRange =
        10.0 / RECORD_SAMPLE_RATE;

    // The complete Record is now a legal X-axis range.
    // Double click also resets to the complete Record.
    currentChartView->configureXNavigation(
        0.0,
        recordDurationSeconds,
        0.0,
        recordDurationSeconds,
        minimumRecordRange,
        recordDurationSeconds);

    // Initial view: about 40 ms.
    const qint64 initialFrames =
        std::min<qint64>(
            recordTotalFrames,
            RECORD_DIRECT_READ_LIMIT);

    const double initialEndTime =
        static_cast<double>(
            initialFrames - 1)
        / RECORD_SAMPLE_RATE;

    currentAxisX->setRange(
        0.0,
        std::max(
            minimumRecordRange,
            initialEndTime));

    recordMode = true;

    qDebug()
        << "RECORD: File size ="
        << recordSource.fileSize();

    qDebug()
        << "RECORD: First frame offset ="
        << recordSource.firstFrameOffset();

    qDebug()
        << "RECORD: Total frames ="
        << recordTotalFrames;

    qDebug()
        << "RECORD: Sample rate ="
        << RECORD_SAMPLE_RATE;

    qDebug()
        << "RECORD: Duration ="
        << recordDurationSeconds
        << "s";

    qDebug()
        << "RECORD: Memory mapped ="
        << recordSource.isMemoryMapped();

    loadVisibleRecordRange();

    QMessageBox::information(
        this,
        "Record Loaded",
        QString(
            "Record parsed successfully.\n\n"
            "Total frames: %1\n"
            "Sample rate: 500000 frames/s\n"
            "Duration: %2 s\n\n"
            "Wide views are automatically sampled to about "
            "%3 points per curve.")
            .arg(recordTotalFrames)
            .arg(
                recordDurationSeconds,
                0,
                'f',
                6)
            .arg(RECORD_TARGET_POINTS));
}


// =============================================================
// Load Record data for the visible X range
//
// <= 20,000 visible frames:
//     one contiguous read, then decimate in memory.
//
// > 20,000 visible frames:
//     memory-mapped strided access, approximately 6,000 frames.
//
// The complete Record is never copied into application memory.
// =============================================================

void MainWindow::loadVisibleRecordRange()
{
    if (!recordMode ||
        !recordSource.isOpen() ||
        recordTotalFrames <= 0)
    {
        return;
    }

    double visibleMin =
        currentAxisX->min();

    double visibleMax =
        currentAxisX->max();

    visibleMin =
        std::max(
            0.0,
            visibleMin);

    visibleMax =
        std::min(
            recordDurationSeconds,
            visibleMax);

    if (visibleMax <= visibleMin) {
        return;
    }

    qint64 firstFrame =
        static_cast<qint64>(
            visibleMin *
            RECORD_SAMPLE_RATE);

    qint64 lastFrame =
        static_cast<qint64>(
            visibleMax *
            RECORD_SAMPLE_RATE);

    firstFrame =
        std::clamp<qint64>(
            firstFrame,
            0,
            recordTotalFrames - 1);

    lastFrame =
        std::clamp<qint64>(
            lastFrame,
            firstFrame,
            recordTotalFrames - 1);

    const qint64 visibleFrameCount =
        lastFrame -
        firstFrame +
        1;

    if (visibleFrameCount <= 0) {
        return;
    }

    // Ceiling division keeps the displayed point count
    // at or below approximately RECORD_TARGET_POINTS.
    qint64 stride = 1;

    if (visibleFrameCount >
        RECORD_TARGET_POINTS)
    {
        stride =
            (
                visibleFrameCount +
                RECORD_TARGET_POINTS -
                1
                )
            /
            RECORD_TARGET_POINTS;
    }

    if (firstFrame == loadedRecordFirstFrame &&
        lastFrame == loadedRecordLastFrame &&
        stride == loadedRecordStride)
    {
        return;
    }

    std::vector<RecordDisplaySample> samples;

    samples.reserve(
        static_cast<std::size_t>(
            std::min<qint64>(
                visibleFrameCount,
                RECORD_TARGET_POINTS + 2)));

    // ---------------------------------------------------------
    // Small / medium range
    // ---------------------------------------------------------

    if (visibleFrameCount <=
        RECORD_DIRECT_READ_LIMIT)
    {
        const std::vector<DataFrame> frames =
            recordSource.readFrames(
                firstFrame,
                visibleFrameCount);

        if (frames.empty())
        {
            qDebug()
            << "RECORD: Direct read failed:"
            << recordSource.lastError();

            return;
        }

        for (qint64 localIndex = 0;
             localIndex <
             static_cast<qint64>(frames.size());
             localIndex += stride)
        {
            RecordDisplaySample sample;

            sample.frameIndex =
                firstFrame +
                localIndex;

            sample.frame =
                frames[
                    static_cast<std::size_t>(
                        localIndex)];

            samples.push_back(sample);
        }

        // Include the exact right edge if it was skipped.
        const qint64 finalLocalIndex =
            static_cast<qint64>(frames.size()) - 1;

        if (finalLocalIndex >= 0)
        {
            const qint64 finalGlobalFrame =
                firstFrame +
                finalLocalIndex;

            if (samples.empty() ||
                samples.back().frameIndex !=
                    finalGlobalFrame)
            {
                RecordDisplaySample sample;

                sample.frameIndex =
                    finalGlobalFrame;

                sample.frame =
                    frames.back();

                samples.push_back(sample);
            }
        }
    }

    // ---------------------------------------------------------
    // Large range
    //
    // RecordDataSource uses QFile::map() when available.
    // It copies only the selected 60-byte frames into a compact
    // buffer and calls DataParser once for the whole selection.
    // ---------------------------------------------------------

    else
    {
        const std::vector<RecordDataSource::SampledFrame>
            sampledFrames =
            recordSource.readFramesStrided(
                firstFrame,
                lastFrame,
                stride,
                true);

        if (sampledFrames.empty())
        {
            qDebug()
            << "RECORD: Strided read failed:"
            << recordSource.lastError();

            return;
        }

        samples.reserve(
            sampledFrames.size());

        for (const RecordDataSource::SampledFrame &sampled :
             sampledFrames)
        {
            RecordDisplaySample sample;

            sample.frameIndex =
                sampled.frameIndex;

            sample.frame =
                sampled.frame;

            samples.push_back(sample);
        }
    }

    if (samples.empty()) {
        return;
    }

    loadedRecordFirstFrame =
        firstFrame;

    loadedRecordLastFrame =
        lastFrame;

    loadedRecordStride =
        stride;

    displayRecordSamples(samples);

    qDebug()
        << "RECORD VIEW:"
        << "first ="
        << firstFrame
        << "last ="
        << lastFrame
        << "visible ="
        << visibleFrameCount
        << "stride ="
        << stride
        << "plotted ="
        << samples.size()
        << "mapped ="
        << recordSource.isMemoryMapped();
}


// =============================================================
// Display sampled Record data
// =============================================================

void MainWindow::displayRecordSamples(
    const std::vector<RecordDisplaySample> &samples)
{
    if (samples.empty()) {
        return;
    }

    QList<QPointF> rawPoints;
    QList<QPointF> firPoints;
    QList<QPointF> peakPoints;

    rawPoints.reserve(
        static_cast<qsizetype>(
            samples.size()));

    firPoints.reserve(
        static_cast<qsizetype>(
            samples.size()));

    peakPoints.reserve(
        static_cast<qsizetype>(
            samples.size()));

    double minY =
        std::numeric_limits<double>::max();

    double maxY =
        std::numeric_limits<double>::lowest();

    for (const RecordDisplaySample &sample :
         samples)
    {
        const DataFrame &frame =
            sample.frame;

        const double x =
            static_cast<double>(
                sample.frameIndex)
            / RECORD_SAMPLE_RATE;

        const double raw =
            static_cast<double>(
                frame.currents[0]);

        const double fir =
            static_cast<double>(
                frame.currents[1]);

        const double peak =
            static_cast<double>(
                frame.currents[2]);

        rawPoints.append(
            QPointF(x, raw));

        firPoints.append(
            QPointF(x, fir));

        peakPoints.append(
            QPointF(x, peak));

        minY =
            std::min(minY, raw);

        minY =
            std::min(minY, fir);

        minY =
            std::min(minY, peak);

        maxY =
            std::max(maxY, raw);

        maxY =
            std::max(maxY, fir);

        maxY =
            std::max(maxY, peak);
    }

    i1Series->replace(rawPoints);
    i2Series->replace(firPoints);
    i3Series->replace(peakPoints);

    if (!currentChartView
             ->manualYRangeActive() &&
        minY <= maxY)
    {
        double range =
            maxY - minY;

        if (range < 10.0) {
            range = 10.0;
        }

        const double margin =
            range * 0.10;

        currentAxisY->setRange(
            minY - margin,
            maxY + margin);
    }
}


// =============================================================
// Connect real-time data stream
// =============================================================

void MainWindow::connectDataStream()
{
    // Data connection is allowed only
    // on the Real-time Monitor tab.

    if (ui->mainTabWidget
            ->currentIndex() != 0)
    {
        return;
    }


    // User must explicitly request
    // the real-time connection.

    if (!ui->connectSwitch
             ->isChecked())
    {
        return;
    }


    // Avoid duplicate connection attempts.

    if (dataSocket->state() !=
        QAbstractSocket::UnconnectedState)
    {
        return;
    }


    qDebug()
        << "DATA: Connecting...";


    dataSocket->connectToHost(
        "192.168.7.2",
        8888);
}


// =============================================================
// Disconnect real-time data stream
// =============================================================

void MainWindow::disconnectDataStream()
{
    if (dataSocket->state() ==
        QAbstractSocket::UnconnectedState)
    {
        return;
    }


    qDebug()
        << "DATA: Disconnecting...";


    // This closes ONLY the TCP real-time data connection.
    //
    // It does NOT send a motor STOP command.

    dataSocket->disconnectFromHost();
}


// =============================================================
// Destructor
// =============================================================

MainWindow::~MainWindow()
{
    delete ui;
}