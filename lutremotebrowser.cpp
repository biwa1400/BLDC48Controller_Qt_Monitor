
#include "lutremotebrowser.h"
#include "lutremoteclient.h"
#include "lutcsvparser.h"
#include "lutbinarywriter.h"
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QPainter>
#include <QPen>
#include <algorithm>

#include <QAbstractItemView>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>

LutRemoteBrowser::LutRemoteBrowser(QWidget *parent)
    : QDialog(parent),
    m_client(new LutRemoteClient(this))
{
    setupUi();

    connect(m_client, &LutRemoteClient::filesReceived,
            this, &LutRemoteBrowser::handleFilesReceived);

    connect(m_client, &LutRemoteClient::uploadCompleted,
            this, &LutRemoteBrowser::handleUploadCompleted);

    connect(m_client, &LutRemoteClient::operationFailed,
            this, &LutRemoteBrowser::handleOperationFailed);

    connect(m_client, &LutRemoteClient::busyChanged,
            this, &LutRemoteBrowser::handleBusyChanged);

    connect(m_client, &LutRemoteClient::statusChanged,
            m_statusLabel, &QLabel::setText);

    QTimer::singleShot(0, this, [this]() {
        refreshFiles();
    });
}

void LutRemoteBrowser::setActiveLutName(const QString &fileName)
{
    m_activeLutName = fileName.trimmed();
    updateActiveLutMarker();
}

void LutRemoteBrowser::updateActiveLutMarker()
{
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem *item = m_fileList->item(i);
        const QString name = item->data(Qt::UserRole).toString();
        const bool active = !m_activeLutName.isEmpty() && name == m_activeLutName;
        item->setText(active ? QString::fromUtf8("●  ") + name : QString("    ") + name);
        item->setForeground(active ? QColor(30, 150, 65) : palette().color(QPalette::Text));
        QFont font = item->font();
        font.setBold(active);
        item->setFont(font);
    }
}

void LutRemoteBrowser::setupUi()
{
    setWindowTitle("LUT Manager - ARM Linux");
    resize(720, 520);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel("FPGA LUT File Manager", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(13);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    m_connectionLabel = new QLabel(
        "root@192.168.7.2   /mnt/lut/", this);
    m_connectionLabel->setStyleSheet("color: #666666;");
    mainLayout->addWidget(m_connectionLabel);

    m_statusLabel = new QLabel("Waiting for connection...", this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    auto *listHeader = new QHBoxLayout();

    auto *fileTitle = new QLabel("Remote BIN Files", this);
    QFont headerFont = fileTitle->font();
    headerFont.setBold(true);
    fileTitle->setFont(headerFont);

    m_fileCountLabel = new QLabel("0 files", this);
    m_fileCountLabel->setStyleSheet("color: #777777;");

    listHeader->addWidget(fileTitle);
    listHeader->addStretch();
    listHeader->addWidget(m_fileCountLabel);
    mainLayout->addLayout(listHeader);

    m_fileList = new QListWidget(this);
    m_fileList->setSelectionMode(
        QAbstractItemView::SingleSelection);
    m_fileList->setAlternatingRowColors(true);
    mainLayout->addWidget(m_fileList, 1);

    auto *buttonLayout = new QHBoxLayout();

    m_refreshButton = new QPushButton("Refresh", this);
    m_uploadButton = new QPushButton("Upload CSV", this);
    m_loadButton = new QPushButton("Load to FPGA", this);
    m_closeButton = new QPushButton("Close", this);

    m_loadButton->setEnabled(false);
    m_loadButton->setToolTip("Load selected BIN into FPGA SRAM and activate it.");

    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_uploadButton);
    buttonLayout->addWidget(m_loadButton);
    buttonLayout->addWidget(m_closeButton);

    mainLayout->addLayout(buttonLayout);

    connect(m_refreshButton, &QPushButton::clicked,
            this, &LutRemoteBrowser::refreshFiles);

    connect(m_uploadButton, &QPushButton::clicked,
            this, &LutRemoteBrowser::uploadBin);

    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        if (m_busy || m_selectedRemoteFile.isEmpty())
            return;
        const QByteArray name = m_selectedRemoteFile.toUtf8();
        if (name.isEmpty() || name.size() > 127) {
            QMessageBox::warning(this, "Invalid LUT", "Invalid BIN filename length.");
            return;
        }
        for (char c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
                QMessageBox::warning(this, "Invalid LUT", "The filename contains unsupported characters.");
                return;
            }
        }
        if (!name.endsWith(".bin") || name.startsWith('.')) {
            QMessageBox::warning(this, "Invalid LUT", "Select a valid .bin file.");
            return;
        }
        auto *socket = new QTcpSocket(this);
        m_busy = true;
        m_loadButton->setEnabled(false);
        m_refreshButton->setEnabled(false);
        m_uploadButton->setEnabled(false);
        m_fileList->setEnabled(false);
        m_closeButton->setEnabled(false);
        m_statusLabel->setText("Connecting to ARM for FPGA LUT loading...");
        auto finish = [this, socket](bool ok, const QString &message) {
            if (socket->property("finished").toBool())
                return;
            socket->setProperty("finished", true);
            socket->abort();
            socket->deleteLater();
            m_busy = false;
            m_refreshButton->setEnabled(true);
            m_uploadButton->setEnabled(true);
            m_fileList->setEnabled(true);
            m_closeButton->setEnabled(true);
            m_loadButton->setEnabled(!m_selectedRemoteFile.isEmpty());
            m_statusLabel->setText(message);
            if (ok) {
                m_activeLutName = m_selectedRemoteFile;
                updateActiveLutMarker();
                emit activeLutChanged(m_activeLutName);
                QMessageBox::information(this, "FPGA LUT", message);
            }
            else
                QMessageBox::critical(this, "FPGA LUT", message);
        };
        connect(socket, &QTcpSocket::connected, this, [this, socket, name]() {
            QByteArray frame;
            frame.append(char(0x10));
            frame.append(char(name.size()));
            frame.append(name);
            socket->write(frame);
            m_statusLabel->setText("ARM is loading, verifying and committing the FPGA LUT...");
        });
        connect(socket, &QTcpSocket::readyRead, this, [socket, finish, name]() {
            const QByteArray reply = socket->readAll();
            if (reply.isEmpty())
                return;
            if (static_cast<unsigned char>(reply.at(0)) == 0x00)
                finish(true, QString("FPGA LUT loaded, verified and committed: %1").arg(QString::fromUtf8(name)));
            else
                finish(false, QString("ARM reported LUT_LOAD FAIL: %1").arg(QString::fromUtf8(name)));
        });
        connect(socket, &QTcpSocket::errorOccurred, this, [socket, finish](QAbstractSocket::SocketError) {
            if (!socket->property("finished").toBool())
                finish(false, QString("ARM TCP connection failed: %1").arg(socket->errorString()));
        });
        connect(socket, &QTcpSocket::disconnected, this, [socket, finish]() {
            if (!socket->property("finished").toBool())
                finish(false, "ARM disconnected before reporting LUT_LOAD result.");
        });
        socket->connectToHost("192.168.7.2", 8889);
    });

    connect(m_closeButton, &QPushButton::clicked,
            this, &QDialog::reject);

    connect(m_fileList, &QListWidget::itemSelectionChanged,
            this, &LutRemoteBrowser::updateSelection);

    connect(m_fileList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) {
                updateSelection();
            });
}

void LutRemoteBrowser::refreshFiles()
{
    if (m_client->isBusy())
        return;

    m_remoteFiles.clear();
    m_selectedRemoteFile.clear();

    m_fileList->clear();
    m_fileCountLabel->setText("Loading...");
    m_loadButton->setEnabled(false);

    m_client->refreshFiles();
}

void LutRemoteBrowser::uploadBin()
{
    if (m_busy || m_client->isBusy())
        return;

    const QString csvPath = QFileDialog::getOpenFileName(
        this, "Open FPGA LUT CSV", QString(),
        "CSV Files (*.csv);;All Files (*)");
    if (csvPath.isEmpty())
        return;

    const LutCsvParser::Result parsed = LutCsvParser::parseFile(csvPath);
    if (!parsed.success) {
        QMessageBox::warning(this, "Invalid LUT CSV", parsed.errorMessage);
        return;
    }

    QDialog preview(this);
    preview.setWindowTitle("LUT CSV Preview - " + QFileInfo(csvPath).fileName());
    preview.resize(900, 600);
    preview.setMinimumSize(700, 450);
    auto *layout = new QVBoxLayout(&preview);
    auto *infoGroup = new QGroupBox("LUT Information", &preview);
    auto *infoLayout = new QGridLayout(infoGroup);
    const auto range = std::minmax_element(parsed.data.begin(), parsed.data.end());
    infoLayout->addWidget(new QLabel("CSV file:", infoGroup), 0, 0);
    infoLayout->addWidget(new QLabel(QFileInfo(csvPath).fileName(), infoGroup), 0, 1);
    infoLayout->addWidget(new QLabel("Entries:", infoGroup), 1, 0);
    infoLayout->addWidget(new QLabel("4096 / 4096", infoGroup), 1, 1);
    infoLayout->addWidget(new QLabel("Value range:", infoGroup), 2, 0);
    infoLayout->addWidget(new QLabel(QString("%1 - %2").arg(*range.first).arg(*range.second), infoGroup), 2, 1);
    layout->addWidget(infoGroup);

    auto *chart = new QChart();
    chart->setTitle("LUT Preview");
    chart->legend()->hide();
    auto *series = new QLineSeries(chart);
    series->setPen(QPen(QColor("#2196F3"), 1.5));
    QList<QPointF> points;
    points.reserve(static_cast<qsizetype>(parsed.data.size()));
    for (std::size_t i = 0; i < parsed.data.size(); ++i)
        points.append(QPointF(static_cast<qreal>(i), static_cast<qreal>(parsed.data[i])));
    series->replace(points);
    chart->addSeries(series);
    auto *axisX = new QValueAxis(chart);
    auto *axisY = new QValueAxis(chart);
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
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    auto *chartView = new QChartView(chart, &preview);
    chartView->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(chartView, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &preview);
    connect(buttons, &QDialogButtonBox::accepted, &preview, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &preview, &QDialog::reject);
    layout->addWidget(buttons);
    if (preview.exec() != QDialog::Accepted)
        return;

    const QFileInfo csvInfo(csvPath);
    const QString binPath = csvInfo.absolutePath() + "/" + csvInfo.completeBaseName() + ".bin";
    const LutBinaryWriter::Result result = LutBinaryWriter::writeFile(binPath, parsed.data);
    if (!result.success) {
        QMessageBox::critical(this, "BIN Conversion Failed", result.errorMessage);
        return;
    }
    const QString fileName = QFileInfo(result.filePath).fileName();
    if (m_remoteFiles.contains(fileName)) {
        confirmOverwrite(result.filePath);
        return;
    }
    m_client->uploadBin(result.filePath, false);
}

void LutRemoteBrowser::confirmOverwrite(const QString &localPath)
{
    const QString fileName = QFileInfo(localPath).fileName();

    const QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            "Overwrite Remote LUT",
            QString(
                "The following LUT already exists on ARM Linux:\n\n"
                "%1\n\n"
                "Do you want to replace it?\n\n"
                "This operation changes the Linux file only. "
                "It does not load the LUT into FPGA SRAM.")
                .arg(fileName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    m_client->uploadBin(localPath, true);
}

void LutRemoteBrowser::updateSelection()
{
    QListWidgetItem *item = m_fileList->currentItem();

    if (!item) {
        m_selectedRemoteFile.clear();
        m_loadButton->setEnabled(false);
        return;
    }

    m_selectedRemoteFile = item->data(Qt::UserRole).toString();
    m_loadButton->setEnabled(!m_busy && !m_selectedRemoteFile.isEmpty());
}

void LutRemoteBrowser::handleFilesReceived(
    const QStringList &fileNames)
{
    m_remoteFiles = fileNames;
    m_selectedRemoteFile.clear();

    m_fileList->clear();

    for (const QString &fileName : fileNames) {
        auto *item = new QListWidgetItem(fileName);

        item->setData(Qt::UserRole, fileName);
        item->setToolTip("/mnt/lut/" + fileName);

        m_fileList->addItem(item);
    }

    updateActiveLutMarker();
    m_fileCountLabel->setText(
        QString("%1 file(s)").arg(fileNames.size()));

    if (!fileNames.isEmpty())
        m_fileList->setCurrentRow(0);
}

void LutRemoteBrowser::handleUploadCompleted(
    const QString &remotePath)
{
    QMessageBox::information(
        this,
        "Upload Complete",
        QString(
            "LUT converted and uploaded successfully:\n\n"
            "%1\n\n"
            "The file is stored on ARM Linux.\n"
            "FPGA SRAM has not been modified.")
            .arg(remotePath));

    refreshFiles();
}

void LutRemoteBrowser::handleOperationFailed(
    const QString &message)
{
    m_fileCountLabel->setText(
        QString("%1 file(s)").arg(m_remoteFiles.size()));

    QMessageBox::critical(
        this,
        "LUT Remote Operation Error",
        message);
}

void LutRemoteBrowser::handleBusyChanged(bool busy)
{
    m_busy = busy;

    m_refreshButton->setEnabled(!busy);
    m_uploadButton->setEnabled(!busy);
    m_fileList->setEnabled(!busy);

    m_loadButton->setEnabled(!busy && !m_selectedRemoteFile.isEmpty());

    if (busy)
        m_closeButton->setText("Cancel / Close");
    else
        m_closeButton->setText("Close");
}