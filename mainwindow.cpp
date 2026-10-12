#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "recordfiledialog.h"
#include "recorddatasource.h"
#include "livevalueswindow.h"
#include "thdmonitorwindow.h"
#include "lutremotebrowser.h"

#include <QDebug>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QLegendMarker>
#include <QAbstractSeries>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QTabBar>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
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
    , currentValueTimer(
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


    auto *liveValuesButton =
        new QPushButton(
            "Live Values",
            this);

    liveValuesButton->setMinimumSize(
        110,
        30);

    liveValuesButton->setStyleSheet(
        "QPushButton {"
        "font-weight: bold;"
        "padding: 4px 12px;"
        "}");


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
        liveValuesButton);

    ui->statusLayout->insertWidget(
        pauseIndex + 1,
        readRecordButton);


    // LUT controls: keep the manager entry and FPGA status separate.
    lutManagerButton = new QPushButton("LUT Manager", this);
    lutManagerButton->setMinimumSize(110, 30);
    lutManagerButton->setStyleSheet(
        "QPushButton { font-weight: bold; padding: 4px 12px; }");

    auto *lutStatusTitle = new QLabel("Current FPGA LUT:", this);
    currentFpgaLutLabel = new QLabel("Unknown", this);
    currentFpgaLutLabel->setMinimumWidth(120);
    currentFpgaLutLabel->setToolTip(
        "The active LUT is not yet reported by ARM Linux.");

    ui->statusLayout->insertWidget(pauseIndex + 3, lutManagerButton);
    ui->statusLayout->insertWidget(pauseIndex + 4, lutStatusTitle);
    ui->statusLayout->insertWidget(pauseIndex + 5, currentFpgaLutLabel);

    connect(lutManagerButton, &QPushButton::clicked,
            this, &MainWindow::openLutManager);

    // THD Monitor: an independent TCP connection, active only while open.
    auto *thdMonitorButton = new QPushButton("THD Monitor", this);
    thdMonitorButton->setMinimumSize(110, 30);
    thdMonitorButton->setStyleSheet(
        "QPushButton { font-weight: bold; padding: 4px 12px; }");
    ui->statusLayout->insertWidget(pauseIndex + 2, thdMonitorButton);

    connect(thdMonitorButton, &QPushButton::clicked, this, [this]() {
        if (!thdMonitorWindow) {
            thdMonitorWindow = new ThdMonitorWindow(
                QStringLiteral("192.168.7.2"), nullptr);
            thdMonitorWindow->setAttribute(Qt::WA_DeleteOnClose, true);
            connect(thdMonitorWindow, &QObject::destroyed, this, [this]() {
                thdMonitorWindow = nullptr;
            });
        }
        thdMonitorWindow->show();
        thdMonitorWindow->raise();
        thdMonitorWindow->activateWindow();
    });

    // =========================================================
    // Live Values window
    // =========================================================

    connect(
        liveValuesButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (!liveValuesWindow)
            {
                liveValuesWindow =
                    new LiveValuesWindow(nullptr);

                liveValuesWindow->setAttribute(
                    Qt::WA_DeleteOnClose,
                    true);

                auto *rateLabel = new QLabel("Data updates: 0 Hz", liveValuesWindow);
                rateLabel->setObjectName("liveValuesUpdateRate");
                rateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                rateLabel->setStyleSheet("font-weight: bold; padding: 2px 8px;");
                liveValuesWindow->layout()->addWidget(rateLabel);

                connect(
                    liveValuesWindow,
                    &QObject::destroyed,
                    this,
                    [this]()
                    {
                        liveValuesWindow = nullptr;
                    });
            }

            if (!dataCache.empty()) {
                const DataFrame &frame =
                    dataCache.at(dataCache.size() - 1);

                liveValuesWindow->updateValues(frame);
            }

            liveValuesWindow->show();
            liveValuesWindow->raise();
            liveValuesWindow->activateWindow();
        });


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
        if (controlSocket->state() != QAbstractSocket::ConnectedState)
        {
            qDebug() << "CONTROL:" << name << "reconnecting...";
            if (controlSocket->state() != QAbstractSocket::ConnectingState) {
                controlSocket->abort();
                controlSocket->connectToHost("192.168.7.2", 8889);
            }
            if (!controlSocket->waitForConnected(2000)) {
                QMessageBox::warning(this, "Control Connection Failed",
                                     QString("Cannot send %1: control socket reconnection failed.\n%2")
                                         .arg(QString::fromLatin1(name), controlSocket->errorString()));
                return;
            }
        }

        if (controlSocket->write(&command, 1) != 1) {
            QMessageBox::warning(this, "Control Send Failed",
                                 QString("Cannot send %1: %2")
                                     .arg(QString::fromLatin1(name), controlSocket->errorString()));
            return;
        }
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
    // Current chart: I1 ~ I22 + channel selector
    // =========================================================

    currentChart =
        new QChart();

    currentChart->setTitle("");

    // The channel selector on the right replaces the old legend.
    currentChart
        ->legend()
        ->setVisible(false);

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
        "Current");


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


    // =========================================================
    // Create I1 ~ I22 series
    //
    // All 22 QLineSeries objects are tiny when empty.  Only
    // checked channels receive QPointF history during refresh.
    // =========================================================

    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT;
         ++channel)
    {
        QLineSeries *series =
            new QLineSeries(this);

        series->setName(
            QString("I%1")
                .arg(channel + 1));

        const int hue =
            static_cast<int>(
                (channel * 359) /
                CURRENT_CHANNEL_COUNT);

        series->setPen(
            QPen(
                QColor::fromHsv(
                    hue,
                    210,
                    220),
                2.0));

        currentChart->addSeries(series);

        series->attachAxis(
            currentAxisX);

        series->attachAxis(
            currentAxisY);

        // All Peak channels are visible by default.
        series->setVisible(true);

        currentSeries[channel] =
            series;
    }


    // FIR Filter curves: share the current axes with Peak curves.
    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT; ++channel)
    {
        auto *series = new QLineSeries(this);
        series->setName(QString("F%1").arg(channel + 1));
        const QColor peakColor = currentSeries[channel]->pen().color();
        QPen pen(peakColor, 1.5);
        pen.setStyle(Qt::DashLine);
        series->setPen(pen);
        currentChart->addSeries(series);
        series->attachAxis(currentAxisX);
        series->attachAxis(currentAxisY);
        series->setVisible(true);
        filterSeries[channel] = series;
    }


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


    // Default navigation = 5-second live view.
    currentChartView->configureXNavigation(
        -5.0,
        0.0,
        -5.0,
        0.0,
        0.001,
        5.0);


    // =========================================================
    // Current Channels selector on the RIGHT of the chart
    // =========================================================

    auto *currentArea =
        new QWidget(this);

    auto *currentAreaLayout =
        new QHBoxLayout(currentArea);

    currentAreaLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    currentAreaLayout->setSpacing(8);

    currentAreaLayout->addWidget(
        currentChartView,
        1);


    auto *channelGroup =
        new QGroupBox(
            "Current Channels",
            currentArea);

    channelGroup->setFixedWidth(170);

    auto *channelGroupLayout =
        new QVBoxLayout(channelGroup);

    channelGroupLayout->setContentsMargins(
        6,
        8,
        6,
        6);


    auto *channelScrollArea =
        new QScrollArea(channelGroup);

    channelScrollArea->setWidgetResizable(true);
    channelScrollArea->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);

    channelScrollArea->setFrameShape(
        QFrame::NoFrame);


    auto *channelScrollWidget =
        new QWidget(channelScrollArea);

    auto *channelListLayout =
        new QVBoxLayout(channelScrollWidget);

    channelListLayout->setContentsMargins(
        2,
        2,
        2,
        2);

    channelListLayout->setSpacing(3);


    // Peak group: one tri-state selector controls all 22 channels.
    auto *peakAllCheck = new QCheckBox("Peak Value - Select All", channelScrollWidget);
    peakAllCheck->setTristate(true);
    peakAllCheck->setCheckState(Qt::PartiallyChecked);
    peakAllCheck->setStyleSheet("QCheckBox { font-weight: 700; }");
    channelListLayout->addWidget(peakAllCheck);

    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT;
         ++channel)
    {
        auto *checkBox =
            new QCheckBox(
                QString("I%1")
                    .arg(channel + 1),
                channelScrollWidget);

        checkBox->setChecked(channel == 0);
        currentSeries[channel]->setVisible(channel == 0);

        const QColor channelColor =
            currentSeries[channel]
                ->pen()
                .color();

        checkBox->setStyleSheet(
            QString(
                "QCheckBox { color: %1; font-weight: 600; }")
                .arg(channelColor.name()));

        currentChannelChecks[channel] =
            checkBox;

        auto *channelRow =
            new QWidget(channelScrollWidget);

        auto *channelRowLayout =
            new QHBoxLayout(channelRow);

        channelRowLayout->setContentsMargins(
            0, 0, 0, 0);
        channelRowLayout->setSpacing(4);

        auto *valueLabel =
            new QLabel("0", channelRow);

        valueLabel->setAlignment(
            Qt::AlignRight | Qt::AlignVCenter);
        valueLabel->setMinimumWidth(48);
        valueLabel->setStyleSheet(
            "QLabel { font-family: Consolas; }");

        currentValueLabels[channel] =
            valueLabel;

        channelRowLayout->addWidget(checkBox);
        channelRowLayout->addStretch();
        channelRowLayout->addWidget(valueLabel);

        channelListLayout->addWidget(
            channelRow);

        connect(
            checkBox,
            &QCheckBox::toggled,
            this,
            [this, channel](bool checked)
            {
                QLineSeries *series =
                    currentSeries[channel];

                if (!series) {
                    return;
                }

                series->setVisible(checked);

                if (!checked) {
                    // Release all plotted point memory immediately.
                    series->clear();
                }

                // Record mode is not refreshed by the 20 ms live
                // timer.  Force the visible Record range to reload
                // when a channel selection changes.
                if (recordMode) {
                    loadedRecordFirstFrame = -1;
                    loadedRecordLastFrame = -1;
                    loadedRecordStride = -1;
                    loadVisibleRecordRange();
                }
            });
    }

    auto *filterAllCheck = new QCheckBox("FIR Filter - Select All", channelScrollWidget);
    filterAllCheck->setTristate(true);
    filterAllCheck->setCheckState(Qt::PartiallyChecked);
    filterAllCheck->setStyleSheet("QCheckBox { font-weight: 700; }");
    channelListLayout->addWidget(filterAllCheck);
    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT; ++channel)
    {
        auto *checkBox = new QCheckBox(
            QString("F%1").arg(channel + 1), channelScrollWidget);
        checkBox->setChecked(channel == 0);
        filterSeries[channel]->setVisible(channel == 0);
        checkBox->setStyleSheet(
            QString("QCheckBox { color: %1; font-weight: 600; }")
                .arg(filterSeries[channel]->pen().color().name()));
        filterChannelChecks[channel] = checkBox;
        channelListLayout->addWidget(checkBox);
        connect(checkBox, &QCheckBox::toggled, this,
                [this, channel](bool checked)
                {
                    auto *series = filterSeries[channel];
                    series->setVisible(checked);
                    if (!checked) series->clear();
                    if (recordMode) {
                        loadedRecordFirstFrame = -1;
                        loadedRecordLastFrame = -1;
                        loadedRecordStride = -1;
                        loadVisibleRecordRange();
                    }
                });
    }

    // Group checkboxes follow individual selections (including partial state).
    auto syncPeakAll = [this, peakAllCheck]() {
        int selected = 0;
        for (auto *check : currentChannelChecks)
            if (check && check->isChecked()) ++selected;
        peakAllCheck->setCheckState(selected == 0 ? Qt::Unchecked :
                                        selected == static_cast<int>(CURRENT_CHANNEL_COUNT) ? Qt::Checked :
                                        Qt::PartiallyChecked);
    };
    auto syncFilterAll = [this, filterAllCheck]() {
        int selected = 0;
        for (auto *check : filterChannelChecks)
            if (check && check->isChecked()) ++selected;
        filterAllCheck->setCheckState(selected == 0 ? Qt::Unchecked :
                                          selected == static_cast<int>(CURRENT_CHANNEL_COUNT) ? Qt::Checked :
                                          Qt::PartiallyChecked);
    };
    for (auto *check : currentChannelChecks)
        connect(check, &QCheckBox::toggled, this, [syncPeakAll](bool) { syncPeakAll(); });
    for (auto *check : filterChannelChecks)
        connect(check, &QCheckBox::toggled, this, [syncFilterAll](bool) { syncFilterAll(); });
    connect(peakAllCheck, &QCheckBox::clicked, this, [this, peakAllCheck](bool checked) {
        peakAllCheck->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        for (auto *check : currentChannelChecks)
            if (check) check->setChecked(checked);
    });
    connect(filterAllCheck, &QCheckBox::clicked, this, [this, filterAllCheck](bool checked) {
        filterAllCheck->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        for (auto *check : filterChannelChecks)
            if (check) check->setChecked(checked);
    });

    channelListLayout->addStretch();

    channelScrollArea->setWidget(
        channelScrollWidget);

    channelGroupLayout->addWidget(
        channelScrollArea);

    currentAreaLayout->addWidget(
        channelGroup,
        0);

    ui->currentChartLayout->addWidget(
        currentArea);


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

    // Target display rate = 35 FPS (approximately 28.6 ms).

    displayTimer->setInterval(
        29);


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


            constexpr double DISPLAY_SECONDS = 5.0;
            // TCP delivers frames in bursts. Per-frame PC arrival timestamps
            // are NOT sample timestamps: they create near-vertical segments.
            // Estimate the effective rate from the elapsed time of the full
            // cached window, then distribute samples uniformly in frame order.
            // This preserves waveform shape without assuming 4000 frames/s.
            const std::size_t cacheSize = dataCache.size();
            const std::size_t firstIndex = 0;
            const std::size_t sampleCount = cacheSize;
            const auto latestTime = dataCache.latestTimestamp();
            const double elapsedSeconds = sampleCount > 1
                                              ? std::chrono::duration<double>(
                                                    latestTime - dataCache.timestampAt(firstIndex)).count()
                                              : 0.0;
            const double samplePeriod = (sampleCount > 1 && elapsedSeconds > 0.0)
                                            ? elapsedSeconds / static_cast<double>(sampleCount - 1)
                                            : 0.0;
            auto getX = [sampleCount, samplePeriod](std::size_t i) -> double {
                return -static_cast<double>(sampleCount - 1 - i) * samplePeriod;
            };

            // Avoid rebuilding all series when neither new frames nor the
            // visible time range nor channel selections have changed.
            static quint64 lastRenderedFrame = std::numeric_limits<quint64>::max();
            static double lastXMin = std::numeric_limits<double>::quiet_NaN();
            static double lastXMax = std::numeric_limits<double>::quiet_NaN();
            static quint64 lastSelectionMask = std::numeric_limits<quint64>::max();
            quint64 selectionMask = 0;
            std::array<bool, CURRENT_CHANNEL_COUNT> channelEnabled{};
            std::array<bool, CURRENT_CHANNEL_COUNT> filterEnabled{};
            for (std::size_t ch = 0; ch < CURRENT_CHANNEL_COUNT; ++ch) {
                channelEnabled[ch] = currentChannelChecks[ch] &&
                                     currentChannelChecks[ch]->isChecked();
                filterEnabled[ch] = filterChannelChecks[ch] &&
                                    filterChannelChecks[ch]->isChecked();
                if (channelEnabled[ch]) selectionMask |= quint64(1) << ch;
                if (filterEnabled[ch]) selectionMask |= quint64(1) << (ch + 22);
            }
            const double xMin = currentAxisX->min();
            const double xMax = currentAxisX->max();
            if (receivedFrames == lastRenderedFrame &&
                xMin == lastXMin && xMax == lastXMax &&
                selectionMask == lastSelectionMask) return;
            lastRenderedFrame = receivedFrames;
            lastXMin = xMin;
            lastXMax = xMax;
            lastSelectionMask = selectionMask;

            // Use the existing zoom/pan axis. For every visible pixel bucket,
            // preserve BOTH minimum and maximum in chronological order.
            // Once zoomed in far enough, draw every original sample.
            const int pixelWidth = std::max(200, static_cast<int>(
                                                     currentChartView->chart()->plotArea().width()));
            // Keep approximately two extrema per four screen pixels in the
            // overview. This reduces Qt Charts geometry for 44 simultaneous
            // series without touching the cached raw samples. On zoom-in,
            // drawChannel() automatically switches back to every raw sample.
            const std::size_t bucketCount = static_cast<std::size_t>(
                std::max(100, pixelWidth / 4));
            // Timestamp order is monotonic. Binary search limits work to
            // samples inside the existing mouse zoom/pan X-axis viewport.
            std::size_t visibleFirst = 0;
            std::size_t visibleEnd = sampleCount;
            {
                std::size_t lo = 0, hi = sampleCount;
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    if (getX(mid) < xMin) lo = mid + 1;
                    else hi = mid;
                }
                visibleFirst = lo;
                lo = visibleFirst;
                hi = sampleCount;
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    if (getX(mid) <= xMax) lo = mid + 1;
                    else hi = mid;
                }
                visibleEnd = lo;
            }
            const std::size_t visibleCount = visibleEnd - visibleFirst;
            std::array<QList<QPointF>, CURRENT_CHANNEL_COUNT> currentPoints;
            std::array<QList<QPointF>, CURRENT_CHANNEL_COUNT> filterPoints;
            QList<QPointF> rpmPoints;
            double currentMinY = std::numeric_limits<double>::max();
            double currentMaxY = std::numeric_limits<double>::lowest();
            double rpmMinY = std::numeric_limits<double>::max();
            double rpmMaxY = std::numeric_limits<double>::lowest();
            auto drawChannel = [&](QList<QPointF>& points, auto getValue) {
                if (!visibleCount) return;
                points.reserve(static_cast<qsizetype>(
                    std::min(visibleCount, bucketCount * 2)));
                if (visibleCount <= bucketCount * 2) {
                    for (std::size_t i = visibleFirst; i < visibleEnd; ++i)
                        points.append(QPointF(getX(i), getValue(dataCache.at(firstIndex + i))));
                    return;
                }
                for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
                    const std::size_t begin = visibleFirst + bucket * visibleCount / bucketCount;
                    const std::size_t end = visibleFirst + (bucket + 1) * visibleCount / bucketCount;
                    if (begin >= end) continue;
                    std::size_t minIdx = begin, maxIdx = begin;
                    double minVal = getValue(dataCache.at(firstIndex + begin));
                    double maxVal = minVal;
                    for (std::size_t i = begin + 1; i < end; ++i) {
                        const double v = getValue(dataCache.at(firstIndex + i));
                        if (v < minVal) { minVal = v; minIdx = i; }
                        if (v > maxVal) { maxVal = v; maxIdx = i; }
                    }
                    if (minIdx <= maxIdx) {
                        points.append(QPointF(getX(minIdx), minVal));
                        if (maxIdx != minIdx) points.append(QPointF(getX(maxIdx), maxVal));
                    } else {
                        points.append(QPointF(getX(maxIdx), maxVal));
                        points.append(QPointF(getX(minIdx), minVal));
                    }
                }
            };
            for (std::size_t ch = 0; ch < CURRENT_CHANNEL_COUNT; ++ch) {
                if (channelEnabled[ch]) {
                    drawChannel(currentPoints[ch], [ch](const DataFrame& f) {
                        return static_cast<double>(f.currents[ch]); });
                    for (const auto& pt : currentPoints[ch]) {
                        currentMinY = std::min(currentMinY, pt.y());
                        currentMaxY = std::max(currentMaxY, pt.y());
                    }
                    currentSeries[ch]->replace(currentPoints[ch]);
                }
                if (filterEnabled[ch]) {
                    drawChannel(filterPoints[ch], [ch](const DataFrame& f) {
                        return static_cast<double>(f.filteredCurrents[ch]); });
                    for (const auto& pt : filterPoints[ch]) {
                        currentMinY = std::min(currentMinY, pt.y());
                        currentMaxY = std::max(currentMaxY, pt.y());
                    }
                    filterSeries[ch]->replace(filterPoints[ch]);
                }
            }
            drawChannel(rpmPoints, [](const DataFrame& f) {
                return static_cast<double>(f.rpm); });
            for (const auto& pt : rpmPoints) {
                rpmMinY = std::min(rpmMinY, pt.y());
                rpmMaxY = std::max(rpmMaxY, pt.y());
            }
            rpmSeries->replace(rpmPoints);
            // Avoid a redundant axis change notification and chart relayout.
            if (rpmAxisX->min() != -DISPLAY_SECONDS || rpmAxisX->max() != 0.0) {
                rpmAxisX->setRange(-DISPLAY_SECONDS, 0.0);
            }

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


                const double desiredMin = currentMinY - margin;
                const double desiredMax = currentMaxY + margin;
                // Sub-pixel-sized changes do not justify a full chart axis relayout.
                // Do not apply this when the user has manually selected a Y range.
                const double tolerance = (desiredMax - desiredMin) * 0.001;
                if (std::abs(currentAxisY->min() - desiredMin) > tolerance ||
                    std::abs(currentAxisY->max() - desiredMax) > tolerance) {
                    currentAxisY->setRange(desiredMin, desiredMax);
                }
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


                const double desiredMin = center - range / 2.0 - margin;
                const double desiredMax = center + range / 2.0 + margin;
                const double tolerance = (desiredMax - desiredMin) * 0.001;
                if (std::abs(rpmAxisY->min() - desiredMin) > tolerance ||
                    std::abs(rpmAxisY->max() - desiredMax) > tolerance) {
                    rpmAxisY->setRange(desiredMin, desiredMax);
                }
            }
        });


    displayTimer->start();


    // =========================================================
    // Current channel numeric values (target 50 Hz)
    // =========================================================

    currentValueTimer->setInterval(20);
    currentValueTimer->setProperty("liveFreshUpdates", 0);

    connect(
        currentValueTimer,
        &QTimer::timeout,
        this,
        [this, lastSeenFrameTime = std::chrono::steady_clock::time_point{}]() mutable
        {
            if (dataSocket->state() != QAbstractSocket::ConnectedState || dataCache.empty()) {
                return;
            }

            const auto newestFrameTime = dataCache.latestTimestamp();
            if (newestFrameTime == lastSeenFrameTime) {
                return; // No newly received frame since the last UI update.
            }
            lastSeenFrameTime = newestFrameTime;
            currentValueTimer->setProperty("liveFreshUpdates",
                                           currentValueTimer->property("liveFreshUpdates").toInt() + 1);

            const DataFrame &frame =
                dataCache.at(dataCache.size() - 1);

            for (std::size_t channel = 0;
                 channel < CURRENT_CHANNEL_COUNT;
                 ++channel)
            {
                QLabel *label =
                    currentValueLabels[channel];

                if (!label) {
                    continue;
                }

                label->setText(
                    QString::number(
                        frame.currents[channel]));
            }

            if (liveValuesWindow) {
                liveValuesWindow->updateValues(frame);
            }
        });

    currentValueTimer->start();


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


            // Count only Live Values updates backed by a newly received frame.
            // The 20 ms timer caps this at approximately 50 updates per second.
            const int liveFreshUpdates = currentValueTimer->property("liveFreshUpdates").toInt();
            currentValueTimer->setProperty("liveFreshUpdates", 0);
            if (liveValuesWindow) {
                if (auto *label = liveValuesWindow->findChild<QLabel*>(
                        "liveValuesUpdateRate")) {
                    label->setText(QString("Data updates: %1 Hz")
                                       .arg(liveFreshUpdates));
                }
            }

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
                    << "I1 ="
                    << frame.currents[0]

                    << "I2 ="
                    << frame.currents[1]

                    << "I3 ="
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
    // controlSocket connects automatically at startup.
    // No motor command is sent on connection.
    // RUN 0x01 is sent only when the user clicks RUN.
    //
    // Data socket is controlled separately by Connect.
    // =========================================================

    controlSocket->connectToHost(
        "192.168.7.2",
        8889);
}


// =============================================================
// LUT Manager entry
// =============================================================
void MainWindow::openLutManager()
{
    auto openBrowser = [this]() {
        LutRemoteBrowser dialog(this);
        dialog.setActiveLutName(currentFpgaLutLabel ? currentFpgaLutLabel->property("activeLutFileName").toString() : QString());
        connect(&dialog, &LutRemoteBrowser::activeLutChanged,
                this, &MainWindow::setCurrentFpgaLutName);
        dialog.exec();
    };

    // Pause only the real-time TCP data stream during SSH/SCP operations.
    // The motor control socket remains connected; no STOP command is sent.
    if (dataSocket->state() != QAbstractSocket::UnconnectedState) {
        connect(dataSocket, &QTcpSocket::disconnected,
                this, openBrowser, Qt::SingleShotConnection);
        ui->connectSwitch->setChecked(false);
        disconnectDataStream();
    } else {
        ui->connectSwitch->setChecked(false);
        openBrowser();
    }
}

void MainWindow::setCurrentFpgaLutName(const QString &fileName)
{
    if (!currentFpgaLutLabel) {
        return;
    }
    const QString name = fileName.trimmed();
    QString displayName = name;
    if (displayName.endsWith(".bin", Qt::CaseInsensitive)) {
        displayName.chop(4);
    }
    currentFpgaLutLabel->setProperty("activeLutFileName", name);
    currentFpgaLutLabel->setText(displayName.isEmpty() ? "Unknown" : displayName);
    currentFpgaLutLabel->setToolTip(
        name.isEmpty() ? "The active LUT is not yet reported by ARM Linux."
                       : name);
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
    // It copies only the selected 112-byte frames into a compact
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

    std::array<QList<QPointF>,
               CURRENT_CHANNEL_COUNT>
        currentPoints;

    std::array<bool,
               CURRENT_CHANNEL_COUNT>
        channelEnabled{};

    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT;
         ++channel)
    {
        channelEnabled[channel] =
            currentChannelChecks[channel] &&
            currentChannelChecks[channel]
                ->isChecked();

        if (channelEnabled[channel]) {
            currentPoints[channel].reserve(
                static_cast<qsizetype>(
                    samples.size()));
        }
    }

    std::array<QList<QPointF>, CURRENT_CHANNEL_COUNT> filterPoints;
    std::array<bool, CURRENT_CHANNEL_COUNT> filterEnabled{};
    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT; ++channel)
    {
        filterEnabled[channel] = filterChannelChecks[channel] &&
                                 filterChannelChecks[channel]->isChecked();
        if (filterEnabled[channel])
            filterPoints[channel].reserve(static_cast<qsizetype>(samples.size()));
    }

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

        for (std::size_t channel = 0;
             channel < CURRENT_CHANNEL_COUNT;
             ++channel)
        {
            if (!channelEnabled[channel]) {
                continue;
            }

            const double y =
                static_cast<double>(
                    frame.currents[channel]);

            currentPoints[channel].append(
                QPointF(x, y));

            minY =
                std::min(minY, y);

            maxY =
                std::max(maxY, y);
        }
        for (std::size_t channel = 0;
             channel < CURRENT_CHANNEL_COUNT; ++channel)
        {
            if (!filterEnabled[channel]) continue;
            const double y = static_cast<double>(frame.filteredCurrents[channel]);
            filterPoints[channel].append(QPointF(x, y));
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }

    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT; ++channel)
    {
        if (filterEnabled[channel])
            filterSeries[channel]->replace(filterPoints[channel]);
    }

    for (std::size_t channel = 0;
         channel < CURRENT_CHANNEL_COUNT;
         ++channel)
    {
        if (!channelEnabled[channel]) {
            continue;
        }

        currentSeries[channel]->replace(
            currentPoints[channel]);
    }

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
    delete thdMonitorWindow;
    delete ui;
}