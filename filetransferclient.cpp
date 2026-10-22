
#include "filetransferclient.h"

#include <QFileInfo>
#include <QStringList>

// ============================================================
// Constructor
// ============================================================

FileTransferClient::FileTransferClient(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);

    // --------------------------------------------------------
    // SCP process finished
    // --------------------------------------------------------

    connect(
        &m_process,
        &QProcess::finished,
        this,
        [this](int exitCode, QProcess::ExitStatus status)
        {
            m_timeout.stop();

            const bool timedOut =
                m_process.property("scpTimedOut").toBool();

            const bool cancelled =
                m_process.property("scpCancelled").toBool();

            const QString output =
                QString::fromUtf8(
                    m_process.readAllStandardOutput());

            QString error =
                QString::fromUtf8(
                    m_process.readAllStandardError());

            const bool success =
                !timedOut &&
                !cancelled &&
                status == QProcess::NormalExit &&
                exitCode == 0;

            if (timedOut) {
                if (!error.isEmpty()) {
                    error += QLatin1Char('\n');
                }

                error += QStringLiteral(
                    "SCP transfer timed out.");
            }

            if (cancelled) {
                if (!error.isEmpty()) {
                    error += QLatin1Char('\n');
                }

                error += QStringLiteral(
                    "SCP transfer cancelled.");
            }

            m_process.setProperty(
                "scpTimedOut", false);

            m_process.setProperty(
                "scpCancelled", false);

            emit transferFinished(
                success,
                output,
                error);
        });

    // --------------------------------------------------------
    // SCP process errors
    // --------------------------------------------------------

    connect(
        &m_process,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error)
        {
            if (error == QProcess::FailedToStart) {
                m_timeout.stop();

                const QString message =
                    QStringLiteral(
                        "Unable to start scp.exe: %1")
                        .arg(m_process.errorString());

                emit transferError(message);

                // FailedToStart may not generate finished().
                // Notify PatchInstaller explicitly.
                emit transferFinished(
                    false,
                    QString(),
                    message);
            }
            else if (error == QProcess::Crashed) {
                // The finished() signal will report failure.
                emit transferError(
                    QStringLiteral(
                        "SCP process crashed."));
            }
        });

    // --------------------------------------------------------
    // SCP timeout
    // --------------------------------------------------------

    connect(
        &m_timeout,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (m_process.state() ==
                QProcess::NotRunning) {
                return;
            }

            m_process.setProperty(
                "scpTimedOut", true);

            emit transferError(
                QStringLiteral(
                    "SCP transfer exceeded the time limit."));

            // Wait for finished() to report final failure.
            m_process.kill();
        });
}

// ============================================================
// Destructor
// ============================================================

FileTransferClient::~FileTransferClient()
{
    m_timeout.stop();

    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}

// ============================================================
// Connection settings
// ============================================================

void FileTransferClient::setHost(const QString &host)
{
    m_host = host.trimmed();
}

void FileTransferClient::setUser(const QString &user)
{
    m_user = user.trimmed();
}

void FileTransferClient::setPort(int port)
{
    m_port = port;
}

void FileTransferClient::setTimeout(int milliseconds)
{
    if (milliseconds > 0) {
        m_timeoutMs = milliseconds;
    }
}

// ============================================================
// Process state
// ============================================================

bool FileTransferClient::isBusy() const
{
    return m_process.state() != QProcess::NotRunning;
}

// ============================================================
// Upload file
// ============================================================

bool FileTransferClient::uploadFile(
    const QString &localPath,
    const QString &remotePath)
{
    const QFileInfo fileInfo(localPath);

    if (!fileInfo.isFile() ||
        fileInfo.isSymLink()) {
        emit transferError(
            QStringLiteral(
                "Local file does not exist or is not a regular file: %1")
                .arg(localPath));

        return false;
    }

    if (remotePath.isEmpty()) {
        emit transferError(
            QStringLiteral(
                "Remote destination is empty."));

        return false;
    }

    return startTransfer(
        fileInfo.absoluteFilePath(),
        m_user + QStringLiteral("@") +
            m_host + QStringLiteral(":") +
            remotePath);
}

// ============================================================
// Download file
// ============================================================

bool FileTransferClient::downloadFile(
    const QString &remotePath,
    const QString &localPath)
{
    if (remotePath.isEmpty() ||
        localPath.isEmpty()) {
        emit transferError(
            QStringLiteral(
                "Invalid transfer path."));

        return false;
    }

    return startTransfer(
        m_user + QStringLiteral("@") +
            m_host + QStringLiteral(":") +
            remotePath,
        localPath);
}

// ============================================================
// Start SCP transfer
// ============================================================

bool FileTransferClient::startTransfer(
    const QString &source,
    const QString &destination)
{
    if (isBusy()) {
        return false;
    }

    if (m_host.isEmpty() ||
        m_user.isEmpty() ||
        m_port < 1 ||
        m_port > 65535 ||
        source.isEmpty() ||
        destination.isEmpty()) {
        return false;
    }

    m_process.setProperty(
        "scpTimedOut", false);

    m_process.setProperty(
        "scpCancelled", false);

    const QStringList args{
        QStringLiteral("-B"),

        QStringLiteral("-o"),
        QStringLiteral("BatchMode=yes"),

        QStringLiteral("-o"),
        QStringLiteral("ConnectTimeout=5"),

        QStringLiteral("-o"),
        QStringLiteral(
            "StrictHostKeyChecking=accept-new"),

        QStringLiteral("-P"),
        QString::number(m_port),

        source,
        destination
    };

    m_process.start(
        QStringLiteral("scp.exe"),
        args);

    // Starting a QProcess is asynchronous.
    // FailedToStart is reported through transferFinished.
    m_timeout.start(m_timeoutMs);

    emit transferStarted(source, destination);

    return true;
}

// ============================================================
// Cancel transfer
// ============================================================

void FileTransferClient::cancel()
{
    if (m_process.state() ==
        QProcess::NotRunning) {
        return;
    }

    m_timeout.stop();

    m_process.setProperty(
        "scpCancelled", true);

    // The finished() callback will report failure.
    m_process.kill();
}
