
#include "lutmanagerdialog.h"
#include "lutbinarywriter.h"

#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QLegend>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cstdint>

LutManagerDialog::LutManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
}

void LutManagerDialog::setupUi()
{
    setWindowTitle("LUT Manager");
    resize(900, 600);
    setMinimumSize(700, 450);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    auto *fileLayout = new QHBoxLayout();

    openCsvButton = new QPushButton("Open CSV", this);
    openCsvButton->setMinimumWidth(120);

    csvFileLabel = new QLabel("No file selected", this);
    csvFileLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    csvFileLabel->setWordWrap(true);

    fileLayout->addWidget(openCsvButton);
    fileLayout->addWidget(csvFileLabel, 1);
    mainLayout->addLayout(fileLayout);

    auto *infoGroup = new QGroupBox("LUT Information", this);
    auto *infoLayout = new QGridLayout(infoGroup);

    entryCountLabel = new QLabel("0 / 4096", infoGroup);
    valueRangeLabel = new QLabel("-", infoGroup);

    infoLayout->addWidget(new QLabel("Entries:", infoGroup), 0, 0);
    infoLayout->addWidget(entryCountLabel, 0, 1);
    infoLayout->addWidget(new QLabel("Value range:", infoGroup), 1, 0);
    infoLayout->addWidget(valueRangeLabel, 1, 1);
    infoLayout->setColumnStretch(1, 1);

    mainLayout->addWidget(infoGroup);

    auto *chart = new QChart();
    chart->setTitle("LUT Preview");
    chart->legend()->hide();

    lutSeries = new QLineSeries(chart);
    lutSeries->setPen(QPen(QColor("#2196F3"), 1.5));
    chart->addSeries(lutSeries);

    axisX = new QValueAxis(chart);
    axisY = new QValueAxis(chart);

    axisX->setTitleText("LUT Index");
    axisX->setRange(0, 4095);
    axisX->setTickCount(9);
    axisX->setLabelFormat("%.0f");

    axisY->setTitleText("Value");
    axisY->setRange(0, 8191);
    axisY->setTickCount(5);
    axisY->setLabelFormat("%.0f");

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);

    lutSeries->attachAxis(axisX);
    lutSeries->attachAxis(axisY);

    chartView = new QChartView(chart, this);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(250);

    mainLayout->addWidget(chartView, 1);

    statusLabel = new QLabel("Waiting for CSV file", this);
    mainLayout->addWidget(statusLabel);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    okButton = new QPushButton("OK", this);
    cancelButton = new QPushButton("Cancel", this);

    okButton->setMinimumWidth(100);
    cancelButton->setMinimumWidth(100);
    okButton->setEnabled(false);
    okButton->setDefault(true);

    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(openCsvButton, &QPushButton::clicked,
            this, &LutManagerDialog::openCsv);
    connect(okButton, &QPushButton::clicked,
            this, &LutManagerDialog::confirmLut);
    connect(cancelButton, &QPushButton::clicked,
            this, &QDialog::reject);
}

void LutManagerDialog::openCsv()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Open LUT CSV",
        currentCsvPath.isEmpty()
            ? QString()
            : QFileInfo(currentCsvPath).absolutePath(),
        "CSV Files (*.csv);;All Files (*)");

    if (filePath.isEmpty())
        return;

    dataValid = false;
    okButton->setEnabled(false);
    currentCsvPath.clear();
    lutSeries->clear();

    const LutCsvParser::Result result =
        LutCsvParser::parseFile(filePath);

    if (!result.success) {
        csvFileLabel->setText(QFileInfo(filePath).fileName());
        csvFileLabel->setToolTip(filePath);
        entryCountLabel->setText("Invalid");
        valueRangeLabel->setText("-");
        statusLabel->setText("CSV validation failed");

        QMessageBox::warning(
            this, "Invalid LUT CSV", result.errorMessage);
        return;
    }

    currentCsvPath = filePath;
    lutData = result.data;
    dataValid = true;

    csvFileLabel->setText(QFileInfo(filePath).fileName());
    csvFileLabel->setToolTip(filePath);
    entryCountLabel->setText(
        QString("%1 / %1").arg(LutCsvParser::EntryCount));

    const auto [minIt, maxIt] =
        std::minmax_element(lutData.begin(), lutData.end());

    valueRangeLabel->setText(
        QString("%1 - %2").arg(*minIt).arg(*maxIt));

    updatePreview();
    okButton->setEnabled(true);
    statusLabel->setText("CSV validated successfully");
}

void LutManagerDialog::updatePreview()
{
    if (!dataValid)
        return;

    QList<QPointF> points;
    points.reserve(static_cast<qsizetype>(lutData.size()));

    for (std::size_t i = 0; i < lutData.size(); ++i) {
        points.append(QPointF(
            static_cast<qreal>(i),
            static_cast<qreal>(lutData[i])));
    }

    lutSeries->replace(points);
}

void LutManagerDialog::confirmLut()
{
    if (!dataValid || currentCsvPath.isEmpty())
        return;

    const QFileInfo csvInfo(currentCsvPath);
    const QString outputPath =
        csvInfo.absolutePath() + "/" + csvInfo.completeBaseName() + ".bin";

    const LutBinaryWriter::Result result =
        LutBinaryWriter::writeFile(outputPath, lutData);

    if (!result.success) {
        QMessageBox::critical(
            this, "BIN Conversion Failed", result.errorMessage);
        statusLabel->setText("BIN conversion failed");
        return;
    }

    statusLabel->setText(
        QString("BIN created: %1 (%2 bytes)")
            .arg(QFileInfo(result.filePath).fileName())
            .arg(result.bytesWritten));

    QMessageBox::information(
        this,
        "LUT Converted",
        QString("BIN file created successfully.\n\n"
                "File: %1\nSize: %2 bytes\n\n"
                "FPGA loading is not implemented yet.")
            .arg(result.filePath)
            .arg(result.bytesWritten));

    accept();
}
