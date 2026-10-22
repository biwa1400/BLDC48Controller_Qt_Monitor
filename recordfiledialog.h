#ifndef RECORDFILEDIALOG_H
#define RECORDFILEDIALOG_H

#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QDialog>
#include <algorithm>

// All implementation is intentionally kept in this header.
// No recordfiledialog.cpp or CMake change is needed.
class RecordFileDialog : public QDialog
{
public:
    explicit RecordFileDialog(QWidget *parent = nullptr)
        : QDialog(parent), m_process(new QProcess(this))
    {
        setWindowTitle(QStringLiteral("Read Record"));
        resize(720, 520);

        auto *mainLayout = new QVBoxLayout(this);
        auto *titleLabel = new QLabel(QStringLiteral("FPGA SoC Record Files"), this);
        QFont titleFont = titleLabel->font();
        titleFont.setPointSize(13);
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);
        mainLayout->addWidget(titleLabel);

        auto *connectionLabel = new QLabel(
            QStringLiteral("root@192.168.7.2   /mnt/data"), this);
        connectionLabel->setStyleSheet(QStringLiteral("color: #555555;"));
        mainLayout->addWidget(connectionLabel);

        m_statusLabel = new QLabel(QStringLiteral("Waiting..."), this);
        m_statusLabel->setStyleSheet(QStringLiteral("color: #666666;"));
        mainLayout->addWidget(m_statusLabel);

        m_progressBar = new QProgressBar(this);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_progressBar->setTextVisible(true);
        m_progressBar->hide();
        mainLayout->addWidget(m_progressBar);

        m_fileList = new QListWidget(this);
        m_fileList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_fileList->setAlternatingRowColors(true);
        mainLayout->addWidget(m_fileList, 1);

        auto *bottomLayout = new QHBoxLayout;
        m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
        m_cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
        m_openButton = new QPushButton(QStringLiteral("Open"), this);
        m_openButton->setDefault(true);
        m_openButton->setEnabled(false);
        bottomLayout->addWidget(m_refreshButton);
        bottomLayout->addStretch();
        bottomLayout->addWidget(m_cancelButton);
        bottomLayout->addWidget(m_openButton);
        mainLayout->addLayout(bottomLayout);

        m_progressTimer.setInterval(200);
        connect(&m_progressTimer, &QTimer::timeout, this,
                [this]() { updateDownloadProgress(); });

        connect(m_refreshButton, &QPushButton::clicked, this,
                [this]() { refreshFiles(); });
        connect(m_openButton, &QPushButton::clicked, this,
                [this]() { openSelectedFile(); });
        connect(m_fileList, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem *) { openSelectedFile(); });
        connect(m_fileList, &QListWidget::itemSelectionChanged, this,
                [this]() {
                    if (m_mode == ProcessMode::None)
                        m_openButton->setEnabled(m_fileList->currentItem() != nullptr);
                });
        connect(m_cancelButton, &QPushButton::clicked, this,
                [this]() {
                    m_cancelling = true;
                    m_progressTimer.stop();
                    if (m_process->state() != QProcess::NotRunning) {
                        m_process->kill();
                        m_process->waitForFinished(1000);
                    }
                    // Only delete the partial file, never a valid cached record.
                    if (!m_partialPath.isEmpty())
                        QFile::remove(m_partialPath);
                    reject();
                });

        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus status) {
                    if (m_cancelling)
                        return;
                    switch (m_mode) {
                    case ProcessMode::ListFiles:
                        handleListFinished(exitCode, status);
                        break;
                    case ProcessMode::QuerySize:
                        handleSizeFinished(exitCode, status);
                        break;
                    case ProcessMode::Download:
                        handleDownloadFinished(exitCode, status);
                        break;
                    case ProcessMode::None:
                        break;
                    }
                });

        connect(m_process, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError error) {
                    if (m_cancelling || error != QProcess::FailedToStart)
                        return;
                    const ProcessMode failedMode = m_mode;
                    m_mode = ProcessMode::None;
                    m_progressTimer.stop();
                    m_progressBar->hide();
                    setBusy(false);
                    const QString program = failedMode == ProcessMode::Download
                                                ? QStringLiteral("scp.exe") : QStringLiteral("ssh.exe");
                    m_statusLabel->setText(program + QStringLiteral(" could not be started."));
                    m_statusLabel->setStyleSheet(
                        QStringLiteral("color: #D32F2F; font-weight: bold;"));
                    if (failedMode == ProcessMode::Download && !m_partialPath.isEmpty())
                        QFile::remove(m_partialPath);
                    QMessageBox::critical(this, QStringLiteral("OpenSSH Error"),
                                          QStringLiteral("%1 could not be started.\n\n%2")
                                              .arg(program, m_process->errorString()));
                });

        QTimer::singleShot(0, this, [this]() { refreshFiles(); });
    }

    QString selectedRemotePath() const { return m_selectedRemotePath; }
    QString downloadedLocalPath() const { return m_downloadedLocalPath; }

private:
    enum class ProcessMode { None, ListFiles, QuerySize, Download };

    static QStringList sshOptions()
    {
        return {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5"),
                QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new")};
    }

    static QString shellQuote(QString value)
    {
        value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return QStringLiteral("'") + value + QStringLiteral("'");
    }

    static QString displayBytes(qint64 bytes)
    {
        if (bytes < 1024)
            return QStringLiteral("%1 B").arg(bytes);
        if (bytes < 1024 * 1024)
            return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        if (bytes < 1024LL * 1024 * 1024)
            return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
        return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
    }

    void setBusy(bool busy)
    {
        m_refreshButton->setEnabled(!busy);
        m_fileList->setEnabled(!busy);
        m_openButton->setEnabled(!busy && m_fileList->currentItem() != nullptr);
    }

    QString recordCacheDirectory() const
    {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("BLDC48Controller_PC_Monitor/records"));
    }

    void refreshFiles()
    {
        if (m_process->state() != QProcess::NotRunning || m_mode != ProcessMode::None)
            return;

        m_fileList->clear();
        m_selectedFileName.clear();
        m_selectedRemotePath.clear();
        m_downloadedLocalPath.clear();
        m_partialPath.clear();
        setBusy(true);
        m_progressBar->hide();
        m_statusLabel->setText(
            QStringLiteral("Connecting to FPGA SoC and reading /mnt/data ..."));
        m_statusLabel->setStyleSheet(
            QStringLiteral("color: #1565C0; font-weight: bold;"));

        QStringList args = sshOptions();
        args << QStringLiteral("root@192.168.7.2")
             << QStringLiteral("LC_ALL=C ls -1Ap /mnt/data");
        m_mode = ProcessMode::ListFiles;
        m_process->start(QStringLiteral("ssh.exe"), args);
    }

    void handleListFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        const QString output = QString::fromUtf8(m_process->readAllStandardOutput());
        const QString error = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        m_mode = ProcessMode::None;
        setBusy(false);

        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            m_statusLabel->setText(QStringLiteral("SSH connection failed."));
            m_statusLabel->setStyleSheet(
                QStringLiteral("color: #D32F2F; font-weight: bold;"));
            QMessageBox::critical(this, QStringLiteral("SSH Error"),
                                  QStringLiteral("Could not read root@192.168.7.2:/mnt/data\n\n%1")
                                      .arg(error.isEmpty() ? QStringLiteral("ssh.exe returned an error.") : error));
            return;
        }

        QStringList names;
        for (QString name : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            name = name.trimmed();
            if (name.isEmpty() || name.endsWith(QLatin1Char('/')) ||
                name == QStringLiteral(".") || name == QStringLiteral("..") ||
                name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
                continue;
            names.append(name);
        }
        names.sort(Qt::CaseInsensitive);

        const QString cache = recordCacheDirectory();
        for (const QString &name : names) {
            const QString path = QDir(cache).filePath(name);
            const QFileInfo info(path);
            const bool local = info.isFile() && info.size() > 0;
            auto *item = new QListWidgetItem(
                local ? name + QStringLiteral("    [Local]") : name);
            item->setData(Qt::UserRole, name);
            item->setToolTip(local
                                 ? QStringLiteral("Local cached copy:\n%1").arg(path)
                                 : QStringLiteral("Remote file:\n/mnt/data/%1").arg(name));
            m_fileList->addItem(item);
        }

        if (names.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("SSH connected. /mnt/data contains no files."));
            m_statusLabel->setStyleSheet(QStringLiteral("color: #666666;"));
        } else {
            m_statusLabel->setText(
                QStringLiteral("SSH connected. %1 file(s) found.").arg(names.size()));
            m_statusLabel->setStyleSheet(
                QStringLiteral("color: #18A538; font-weight: bold;"));
            m_fileList->setCurrentRow(0);
            m_fileList->setFocus();
        }
    }

    void openSelectedFile()
    {
        if (m_mode != ProcessMode::None || m_process->state() != QProcess::NotRunning)
            return;
        QListWidgetItem *item = m_fileList->currentItem();
        if (!item)
            return;
        m_selectedFileName = item->data(Qt::UserRole).toString();
        if (m_selectedFileName.isEmpty() || m_selectedFileName.contains(QLatin1Char('/')) ||
            m_selectedFileName.contains(QLatin1Char('\\')))
            return;

        m_selectedRemotePath = QStringLiteral("/mnt/data/") + m_selectedFileName;
        const QString cache = recordCacheDirectory();
        if (!QDir().mkpath(cache)) {
            QMessageBox::critical(this, QStringLiteral("Record Error"),
                                  QStringLiteral("Could not create local record directory:\n%1").arg(cache));
            return;
        }
        m_downloadedLocalPath = QDir(cache).filePath(m_selectedFileName);
        m_partialPath = m_downloadedLocalPath + QStringLiteral(".part");

        const QFileInfo local(m_downloadedLocalPath);
        if (local.isFile() && local.size() > 0) {
            m_progressBar->hide();
            m_statusLabel->setText(QStringLiteral("Using local copy."));
            m_statusLabel->setStyleSheet(
                QStringLiteral("color: #18A538; font-weight: bold;"));
            QTimer::singleShot(100, this, [this]() { accept(); });
            return;
        }
        queryRemoteSize();
    }

    void queryRemoteSize()
    {
        setBusy(true);
        m_expectedBytes = -1;
        m_progressBar->setRange(0, 0);
        m_progressBar->show();
        m_statusLabel->setText(
            QStringLiteral("Checking file size: %1 ...").arg(m_selectedFileName));
        m_statusLabel->setStyleSheet(
            QStringLiteral("color: #1565C0; font-weight: bold;"));

        QStringList args = sshOptions();
        // BusyBox supports wc -c; no stat or GNU coreutils required.
        args << QStringLiteral("root@192.168.7.2")
             << (QStringLiteral("wc -c < ") + shellQuote(m_selectedRemotePath));
        m_mode = ProcessMode::QuerySize;
        m_process->start(QStringLiteral("ssh.exe"), args);
    }

    void handleSizeFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        const QString output = QString::fromUtf8(m_process->readAllStandardOutput()).trimmed();
        bool ok = false;
        const qint64 size = output.toLongLong(&ok);
        if (exitStatus == QProcess::NormalExit && exitCode == 0 && ok && size >= 0)
            m_expectedBytes = size;
        else
            m_expectedBytes = -1; // Still allow download; show unknown progress.
        m_mode = ProcessMode::None;
        startDownload();
    }

    void startDownload()
    {
        if (m_selectedRemotePath.isEmpty() || m_downloadedLocalPath.isEmpty())
            return;

        // Remove only a previous incomplete .part file.
        if (QFileInfo::exists(m_partialPath) && !QFile::remove(m_partialPath)) {
            m_mode = ProcessMode::None;
            setBusy(false);
            m_progressBar->hide();
            QMessageBox::critical(this, QStringLiteral("Download Error"),
                                  QStringLiteral("Cannot remove previous incomplete download:\n%1")
                                      .arg(m_partialPath));
            return;
        }

        setBusy(true);
        m_progressBar->show();
        if (m_expectedBytes >= 0) {
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(0);
        } else {
            m_progressBar->setRange(0, 0);
        }
        m_statusLabel->setText(
            QStringLiteral("Downloading %1 ...").arg(m_selectedFileName));
        m_statusLabel->setStyleSheet(
            QStringLiteral("color: #1565C0; font-weight: bold;"));

        QStringList args = {QStringLiteral("-P"), QStringLiteral("22")};
        args.append(sshOptions());
        args << (QStringLiteral("root@192.168.7.2:") + m_selectedRemotePath)
             << m_partialPath;

        m_downloadClock.start();
        m_mode = ProcessMode::Download;
        m_process->start(QStringLiteral("scp.exe"), args);
        m_progressTimer.start();
    }

    void updateDownloadProgress()
    {
        if (m_mode != ProcessMode::Download)
            return;

        const QFileInfo partial(m_partialPath);
        const qint64 received = partial.isFile() ? partial.size() : 0;
        const double seconds = std::max(0.001, m_downloadClock.elapsed() / 1000.0);
        const double bytesPerSecond = received / seconds;

        QString detail = QStringLiteral("%1 downloaded").arg(displayBytes(received));
        if (m_expectedBytes >= 0) {
            const int percent = m_expectedBytes == 0 ? 100
                                                     : static_cast<int>(std::min<qint64>(100,
                                                                                         (received * 100) / m_expectedBytes));
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(percent);
            detail = QStringLiteral("%1 / %2 (%3%)")
                         .arg(displayBytes(received), displayBytes(m_expectedBytes))
                         .arg(percent);
        }
        if (received > 0) {
            detail += QStringLiteral("  |  %1/s").arg(displayBytes(
                static_cast<qint64>(bytesPerSecond)));
            if (m_expectedBytes > received && bytesPerSecond > 0) {
                const qint64 remaining = static_cast<qint64>(
                    (m_expectedBytes - received) / bytesPerSecond);
                detail += QStringLiteral("  |  ~%1 s remaining").arg(remaining);
            }
        }
        m_statusLabel->setText(
            QStringLiteral("Downloading %1 ...  %2").arg(m_selectedFileName, detail));
    }

    void handleDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        m_progressTimer.stop();
        updateDownloadProgress();
        const QString error = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        m_mode = ProcessMode::None;
        setBusy(false);

        const QFileInfo partial(m_partialPath);
        const bool validSize = m_expectedBytes < 0 ||
                               (partial.isFile() && partial.size() == m_expectedBytes);
        if (exitStatus != QProcess::NormalExit || exitCode != 0 ||
            !partial.isFile() || !validSize) {
            QFile::remove(m_partialPath);
            m_progressBar->hide();
            m_statusLabel->setText(QStringLiteral("Download failed."));
            m_statusLabel->setStyleSheet(
                QStringLiteral("color: #D32F2F; font-weight: bold;"));
            QString details = error;
            if (!validSize)
                details += QStringLiteral("\nDownloaded size does not match remote file size.");
            if (details.isEmpty())
                details = QStringLiteral("SCP failed or the downloaded file is missing.");
            QMessageBox::critical(this, QStringLiteral("Download Error"),
                                  QStringLiteral("Could not download:\n%1\n\n%2")
                                      .arg(m_selectedRemotePath, details));
            return;
        }

        // Publish the cache only after SCP completed successfully.
        // QFile::rename() will not overwrite an existing destination.
        if (!QFile::rename(m_partialPath, m_downloadedLocalPath)) {
            m_progressBar->hide();
            m_statusLabel->setText(QStringLiteral("Unable to save downloaded file."));
            QMessageBox::critical(this, QStringLiteral("Download Error"),
                                  QStringLiteral("Cannot move completed download into cache:\n%1\n\n"
                                                 "Partial file retained at:\n%2")
                                      .arg(m_downloadedLocalPath, m_partialPath));
            return;
        }

        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_statusLabel->setText(QStringLiteral("Download complete."));
        m_statusLabel->setStyleSheet(
            QStringLiteral("color: #18A538; font-weight: bold;"));
        QTimer::singleShot(250, this, [this]() { accept(); });
    }

    QProcess *m_process = nullptr;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_fileList = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QTimer m_progressTimer;
    QElapsedTimer m_downloadClock;
    ProcessMode m_mode = ProcessMode::None;
    bool m_cancelling = false;
    qint64 m_expectedBytes = -1;
    QString m_selectedFileName;
    QString m_selectedRemotePath;
    QString m_downloadedLocalPath;
    QString m_partialPath;
};

#endif // RECORDFILEDIALOG_H
