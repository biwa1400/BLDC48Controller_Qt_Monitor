#ifndef RECORDFILEDIALOG_H
#define RECORDFILEDIALOG_H

#include <QDialog>
#include <QProcess>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QAbstractItemView>
#include <QFont>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>


class RecordFileDialog : public QDialog
{
public:

    explicit RecordFileDialog(
        QWidget *parent = nullptr)
        : QDialog(parent)
        , m_process(new QProcess(this))
    {
        setWindowTitle(
            "Read Record");

        resize(
            720,
            520);


        auto *mainLayout =
            new QVBoxLayout(this);


        // =====================================================
        // Title
        // =====================================================

        auto *titleLabel =
            new QLabel(
                "FPGA SoC Record Files",
                this);


        QFont titleFont =
            titleLabel->font();

        titleFont.setPointSize(
            13);

        titleFont.setBold(
            true);

        titleLabel->setFont(
            titleFont);


        mainLayout->addWidget(
            titleLabel);


        // =====================================================
        // Connection information
        // =====================================================

        auto *connectionLabel =
            new QLabel(
                "root@192.168.7.2   /mnt/data",
                this);


        connectionLabel->setStyleSheet(
            "color: #555555;");


        mainLayout->addWidget(
            connectionLabel);


        // =====================================================
        // Status
        // =====================================================

        m_statusLabel =
            new QLabel(
                "Waiting...",
                this);


        m_statusLabel->setStyleSheet(
            "color: #666666;");


        mainLayout->addWidget(
            m_statusLabel);


        // =====================================================
        // Progress bar
        // =====================================================

        m_progressBar =
            new QProgressBar(
                this);


        m_progressBar->setRange(
            0,
            100);

        m_progressBar->setValue(
            0);

        m_progressBar->setTextVisible(
            true);

        m_progressBar->hide();


        mainLayout->addWidget(
            m_progressBar);


        // =====================================================
        // Remote file list
        // =====================================================

        m_fileList =
            new QListWidget(
                this);


        m_fileList->setSelectionMode(
            QAbstractItemView::SingleSelection);

        m_fileList->setAlternatingRowColors(
            true);


        mainLayout->addWidget(
            m_fileList,
            1);


        // =====================================================
        // Buttons
        // =====================================================

        auto *bottomLayout =
            new QHBoxLayout();


        m_refreshButton =
            new QPushButton(
                "Refresh",
                this);


        bottomLayout->addWidget(
            m_refreshButton);


        bottomLayout->addStretch();


        m_cancelButton =
            new QPushButton(
                "Cancel",
                this);


        m_openButton =
            new QPushButton(
                "Open",
                this);


        m_openButton->setDefault(
            true);

        m_openButton->setEnabled(
            false);


        bottomLayout->addWidget(
            m_cancelButton);

        bottomLayout->addWidget(
            m_openButton);


        mainLayout->addLayout(
            bottomLayout);


        // =====================================================
        // Refresh
        // =====================================================

        connect(
            m_refreshButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                refreshFiles();
            });


        // =====================================================
        // Cancel
        // =====================================================

        connect(
            m_cancelButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                if (m_process->state() !=
                    QProcess::NotRunning)
                {
                    m_process->kill();

                    m_process->waitForFinished(
                        1000);
                }


                reject();
            });


        // =====================================================
        // Open
        // =====================================================

        connect(
            m_openButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                openSelectedFile();
            });


        // =====================================================
        // Selection changed
        // =====================================================

        connect(
            m_fileList,
            &QListWidget::itemSelectionChanged,
            this,
            [this]()
            {
                if (m_mode ==
                    ProcessMode::None)
                {
                    m_openButton->setEnabled(
                        m_fileList->currentItem()
                        != nullptr);
                }
            });


        // =====================================================
        // Double click
        // =====================================================

        connect(
            m_fileList,
            &QListWidget::itemDoubleClicked,
            this,
            [this](QListWidgetItem *)
            {
                openSelectedFile();
            });


        // =====================================================
        // Process finished
        // =====================================================

        connect(
            m_process,
            &QProcess::finished,
            this,
            [this](
                int exitCode,
                QProcess::ExitStatus exitStatus)
            {
                if (m_mode ==
                    ProcessMode::ListFiles)
                {
                    handleListFinished(
                        exitCode,
                        exitStatus);
                }
                else if (m_mode ==
                         ProcessMode::Download)
                {
                    handleDownloadFinished(
                        exitCode,
                        exitStatus);
                }
            });


        // =====================================================
        // Process error
        // =====================================================

        connect(
            m_process,
            &QProcess::errorOccurred,
            this,
            [this](
                QProcess::ProcessError error)
            {
                if (error !=
                    QProcess::FailedToStart)
                {
                    return;
                }


                const QString program =
                    (m_mode ==
                     ProcessMode::Download)
                        ? "scp.exe"
                        : "ssh.exe";


                m_mode =
                    ProcessMode::None;


                setBusy(
                    false);


                m_progressBar->hide();


                m_statusLabel->setText(
                    QString(
                        "%1 could not be started.")
                        .arg(
                            program));


                m_statusLabel->setStyleSheet(
                    "color: #D32F2F;"
                    "font-weight: bold;");


                QMessageBox::critical(
                    this,
                    "OpenSSH Error",
                    QString(
                        "%1 could not be started.\n\n"
                        "Check that Windows OpenSSH Client "
                        "is installed and available in PATH.")
                        .arg(
                            program));
            });


        // =====================================================
        // Automatically load remote files
        // =====================================================

        QTimer::singleShot(
            0,
            this,
            [this]()
            {
                refreshFiles();
            });
    }


    // =========================================================
    // Selected remote path
    // =========================================================

    QString selectedRemotePath() const
    {
        return m_selectedRemotePath;
    }


    // =========================================================
    // Local file path
    //
    // This may be:
    //
    // 1. An existing cached local file
    // 2. A newly downloaded file
    // =========================================================

    QString downloadedLocalPath() const
    {
        return m_downloadedLocalPath;
    }


private:

    enum class ProcessMode
    {
        None,
        ListFiles,
        Download
    };


    // =========================================================
    // Busy state
    // =========================================================

    void setBusy(
        bool busy)
    {
        m_refreshButton->setEnabled(
            !busy);


        m_fileList->setEnabled(
            !busy);


        if (busy)
        {
            m_openButton->setEnabled(
                false);
        }
        else
        {
            m_openButton->setEnabled(
                m_fileList->currentItem()
                != nullptr);
        }
    }


    // =========================================================
    // Local cache directory
    // =========================================================

    QString recordCacheDirectory() const
    {
        const QString tempRoot =
            QStandardPaths::writableLocation(
                QStandardPaths::TempLocation);


        return QDir(
                   tempRoot)
            .filePath(
                "BLDC48Controller_PC_Monitor/records");
    }


    // =========================================================
    // Fast remote file listing
    //
    // Keep this intentionally simple because the embedded Linux
    // system provides only a minimal command environment.
    // =========================================================

    void refreshFiles()
    {
        if (m_process->state() !=
            QProcess::NotRunning)
        {
            return;
        }


        m_fileList->clear();


        m_selectedFileName.clear();

        m_selectedRemotePath.clear();

        m_downloadedLocalPath.clear();


        setBusy(
            true);


        m_progressBar->hide();


        m_statusLabel->setText(
            "Connecting to FPGA SoC and reading /mnt/data ...");


        m_statusLabel->setStyleSheet(
            "color: #1565C0;"
            "font-weight: bold;");


        QStringList arguments;


        arguments
            << "-o"
            << "BatchMode=yes"

            << "-o"
            << "ConnectTimeout=5"

            << "-o"
            << "StrictHostKeyChecking=accept-new"

            << "root@192.168.7.2"

            << "LC_ALL=C ls -1Ap /mnt/data";


        m_mode =
            ProcessMode::ListFiles;


        m_process->start(
            "ssh.exe",
            arguments);
    }


    // =========================================================
    // File listing finished
    // =========================================================

    void handleListFinished(
        int exitCode,
        QProcess::ExitStatus exitStatus)
    {
        const QString standardOutput =
            QString::fromUtf8(
                m_process
                    ->readAllStandardOutput());


        const QString standardError =
            QString::fromUtf8(
                m_process
                    ->readAllStandardError())
                .trimmed();


        m_mode =
            ProcessMode::None;


        setBusy(
            false);


        // =====================================================
        // SSH failed
        // =====================================================

        if (exitStatus !=
                QProcess::NormalExit ||
            exitCode != 0)
        {
            m_statusLabel->setText(
                "SSH connection failed.");


            m_statusLabel->setStyleSheet(
                "color: #D32F2F;"
                "font-weight: bold;");


            QString details =
                standardError;


            if (details.isEmpty())
            {
                details =
                    "ssh.exe returned an error "
                    "without a message.";
            }


            QMessageBox::critical(
                this,
                "SSH Error",
                QString(
                    "Could not read:\n"
                    "root@192.168.7.2:/mnt/data\n\n"
                    "%1")
                    .arg(
                        details));


            return;
        }


        // =====================================================
        // Parse file names
        // =====================================================

        QStringList fileNames;


        const QStringList lines =
            standardOutput.split(
                '\n',
                Qt::SkipEmptyParts);


        for (QString fileName :
             lines)
        {
            fileName =
                fileName.trimmed();


            if (fileName.isEmpty())
            {
                continue;
            }


            // ls -p appends '/' to directories.
            if (fileName.endsWith('/'))
            {
                continue;
            }


            fileNames.append(
                fileName);
        }


        fileNames.sort(
            Qt::CaseInsensitive);


        // =====================================================
        // Add files to list
        //
        // If a local file with the same filename exists,
        // display [Local].
        // =====================================================

        const QString cacheDirectory =
            recordCacheDirectory();


        for (const QString &fileName :
             fileNames)
        {
            const QString localPath =
                QDir(
                    cacheDirectory)
                    .filePath(
                        fileName);


            const QFileInfo localInfo(
                localPath);


            const bool localExists =
                localInfo.exists() &&
                localInfo.isFile();


            QString displayName =
                fileName;


            if (localExists)
            {
                displayName +=
                    "    [Local]";
            }


            auto *item =
                new QListWidgetItem(
                    displayName);


            // Store the real filename separately from the
            // displayed "[Local]" text.
            item->setData(
                Qt::UserRole,
                fileName);


            if (localExists)
            {
                item->setToolTip(
                    QString(
                        "Local cached copy:\n%1")
                        .arg(
                            localPath));
            }
            else
            {
                item->setToolTip(
                    QString(
                        "Remote file:\n"
                        "/mnt/data/%1")
                        .arg(
                            fileName));
            }


            m_fileList->addItem(
                item);
        }


        // =====================================================
        // Status
        // =====================================================

        if (fileNames.isEmpty())
        {
            m_statusLabel->setText(
                "SSH connected. "
                "/mnt/data contains no files.");


            m_statusLabel->setStyleSheet(
                "color: #666666;");
        }
        else
        {
            m_statusLabel->setText(
                QString(
                    "SSH connected. "
                    "%1 file(s) found.")
                    .arg(
                        fileNames.size()));


            m_statusLabel->setStyleSheet(
                "color: #18A538;"
                "font-weight: bold;");


            m_fileList->setCurrentRow(
                0);


            m_fileList->setFocus();
        }
    }


    // =========================================================
    // Open selected file
    //
    // Cache rule:
    //
    // Same filename exists locally:
    //     -> use local file immediately
    //
    // No local file:
    //     -> download with SCP
    // =========================================================

    void openSelectedFile()
    {
        if (m_process->state() !=
            QProcess::NotRunning)
        {
            return;
        }


        QListWidgetItem *item =
            m_fileList->currentItem();


        if (!item)
        {
            return;
        }


        m_selectedFileName =
            item->data(
                    Qt::UserRole)
                .toString()
                .trimmed();


        if (m_selectedFileName.isEmpty())
        {
            return;
        }


        // =====================================================
        // Remote path
        // =====================================================

        m_selectedRemotePath =
            "/mnt/data/" +
            m_selectedFileName;


        // =====================================================
        // Local cache path
        // =====================================================

        const QString cacheDirectory =
            recordCacheDirectory();


        if (!QDir().mkpath(
                cacheDirectory))
        {
            QMessageBox::critical(
                this,
                "Record Error",
                QString(
                    "Could not create local record directory:\n"
                    "%1")
                    .arg(
                        cacheDirectory));


            return;
        }


        m_downloadedLocalPath =
            QDir(
                cacheDirectory)
                .filePath(
                    m_selectedFileName);


        // =====================================================
        // Local file already exists
        //
        // No SSH metadata query.
        // No SCP download.
        // Use it immediately.
        // =====================================================

        const QFileInfo localInfo(
            m_downloadedLocalPath);


        if (localInfo.exists() &&
            localInfo.isFile())
        {
            qDebug()
            << "RECORD: Using local cached file"
            << m_downloadedLocalPath
            << "size ="
            << localInfo.size();


            m_progressBar->hide();


            m_statusLabel->setText(
                "Using local copy.");


            m_statusLabel->setStyleSheet(
                "color: #18A538;"
                "font-weight: bold;");


            QTimer::singleShot(
                100,
                this,
                [this]()
                {
                    accept();
                });


            return;
        }


        // =====================================================
        // No local copy
        //
        // Download it from the FPGA.
        // =====================================================

        startDownload();
    }


    // =========================================================
    // Start SCP download
    // =========================================================

    void startDownload()
    {
        if (m_selectedRemotePath.isEmpty() ||
            m_downloadedLocalPath.isEmpty())
        {
            return;
        }


        setBusy(
            true);


        m_statusLabel->setText(
            QString(
                "Downloading %1 ...")
                .arg(
                    m_selectedFileName));


        m_statusLabel->setStyleSheet(
            "color: #1565C0;"
            "font-weight: bold;");


        m_progressBar->show();


        // Indeterminate progress.
        m_progressBar->setRange(
            0,
            0);


        const QString remoteSource =
            QString(
                "root@192.168.7.2:%1")
                .arg(
                    m_selectedRemotePath);


        QStringList arguments;


        arguments
            << "-P"
            << "22"

            << "-o"
            << "BatchMode=yes"

            << "-o"
            << "ConnectTimeout=5"

            << "-o"
            << "StrictHostKeyChecking=accept-new"

            << remoteSource

            << m_downloadedLocalPath;


        m_mode =
            ProcessMode::Download;


        qDebug()
            << "RECORD: Starting SCP"
            << remoteSource
            << "->"
            << m_downloadedLocalPath;


        m_process->start(
            "scp.exe",
            arguments);
    }


    // =========================================================
    // Download finished
    // =========================================================

    void handleDownloadFinished(
        int exitCode,
        QProcess::ExitStatus exitStatus)
    {
        const QString standardError =
            QString::fromUtf8(
                m_process
                    ->readAllStandardError())
                .trimmed();


        m_mode =
            ProcessMode::None;


        setBusy(
            false);


        m_progressBar->setRange(
            0,
            100);


        // =====================================================
        // SCP failed
        // =====================================================

        if (exitStatus !=
                QProcess::NormalExit ||
            exitCode != 0)
        {
            m_progressBar->setValue(
                0);


            m_progressBar->hide();


            m_statusLabel->setText(
                "Download failed.");


            m_statusLabel->setStyleSheet(
                "color: #D32F2F;"
                "font-weight: bold;");


            // Remove incomplete download so it will never be
            // mistaken for a valid cached file next time.
            if (!m_downloadedLocalPath.isEmpty())
            {
                QFile::remove(
                    m_downloadedLocalPath);
            }


            QString details =
                standardError;


            if (details.isEmpty())
            {
                details =
                    "scp.exe returned an error "
                    "without a message.";
            }


            QMessageBox::critical(
                this,
                "Download Error",
                QString(
                    "Could not download:\n"
                    "%1\n\n"
                    "%2")
                    .arg(
                        m_selectedRemotePath,
                        details));


            return;
        }


        // =====================================================
        // Verify downloaded file exists
        // =====================================================

        const QFileInfo localFile(
            m_downloadedLocalPath);


        if (!localFile.exists() ||
            !localFile.isFile())
        {
            m_progressBar->setValue(
                0);


            m_progressBar->hide();


            m_statusLabel->setText(
                "Downloaded file was not found.");


            m_statusLabel->setStyleSheet(
                "color: #D32F2F;"
                "font-weight: bold;");


            QMessageBox::critical(
                this,
                "Download Error",
                "SCP finished, but the downloaded "
                "file could not be found.");


            return;
        }


        // =====================================================
        // Success
        // =====================================================

        m_progressBar->setValue(
            100);


        m_statusLabel->setText(
            "Download complete.");


        m_statusLabel->setStyleSheet(
            "color: #18A538;"
            "font-weight: bold;");


        qDebug()
            << "RECORD: Download complete"
            << m_downloadedLocalPath
            << "size ="
            << localFile.size();


        QTimer::singleShot(
            250,
            this,
            [this]()
            {
                accept();
            });
    }


private:

    QProcess *m_process =
        nullptr;


    QLabel *m_statusLabel =
        nullptr;


    QListWidget *m_fileList =
        nullptr;


    QProgressBar *m_progressBar =
        nullptr;


    QPushButton *m_refreshButton =
        nullptr;


    QPushButton *m_cancelButton =
        nullptr;


    QPushButton *m_openButton =
        nullptr;


    ProcessMode m_mode =
        ProcessMode::None;


    QString m_selectedFileName;


    QString m_selectedRemotePath;


    QString m_downloadedLocalPath;
};


#endif // RECORDFILEDIALOG_H