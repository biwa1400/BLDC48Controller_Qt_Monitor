
#include "sshclient.h"

#include <QStringList>
#include <QVariant>

// ============================================================
// Constructor
// ============================================================

SshClient::SshClient(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);

    // Default SSH command timeout: 15 seconds.
    setProperty("sshTimeoutMs", 15000);

    connect(
        &m_process,
        &QProcess::finished,
        this,
        [this](int exitCode, QProcess::ExitStatus status)
        {
            m_timeout.stop();

            const bool timedOut =
                m_process.property("sshTimedOut").toBool();

            const QString output =
                QString::fromUtf8(
                    m_process.readAllStandardOutput());

            QString error =
                QString::fromUtf8(
                    m_process.readAllStandardError());

            const bool success =
                !timedOut &&
                status == QProcess::NormalExit &&
                exitCode == 0;

            if (timedOut) {
                if (!error.isEmpty()) {
                    error += QLatin1Char('\n');
                }

                error += QStringLiteral(
                    "SSH command timed out. "
                    "The remote command may still be running.");
            }

            m_process.setProperty(
                "sshTimedOut", false);

            emit commandFinished(
                success,
                output,
                error);
        });

    // ========================================================
    // Process errors
    // ========================================================

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
                        "Unable to start ssh.exe: %1")
                        .arg(m_process.errorString());

                emit connectionError(message);

                emit commandFinished(
                    false,
                    QString(),
                    message);
            }
            else if (error == QProcess::Crashed) {
                emit connectionError(
                    QStringLiteral("SSH process crashed."));
            }
        });

    // ========================================================
    // Timeout
    // ========================================================

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
                "sshTimedOut", true);

            emit connectionError(
                QStringLiteral(
                    "SSH command exceeded its timeout."));

            // Terminate the local SSH process.
            // The remote command may continue running.
            m_process.kill();
        });
}

// ============================================================
// Destructor
// ============================================================

SshClient::~SshClient()
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

void SshClient::setHost(const QString &host)
{
    m_host = host.trimmed();
}

void SshClient::setUser(const QString &user)
{
    m_user = user.trimmed();
}

void SshClient::setPort(int port)
{
    m_port = port;
}

// ============================================================
// Configurable timeout
// ============================================================

void SshClient::setTimeout(int milliseconds)
{
    if (milliseconds > 0) {
        setProperty("sshTimeoutMs", milliseconds);
    }
}

// ============================================================
// Process state
// ============================================================

bool SshClient::isBusy() const
{
    return m_process.state() != QProcess::NotRunning;
}

// ============================================================
// Execute SSH command
// ============================================================

bool SshClient::execute(const QString &command)
{
    if (isBusy()) {
        return false;
    }

    if (m_host.isEmpty() ||
        m_user.isEmpty() ||
        m_port < 1 ||
        m_port > 65535 ||
        command.trimmed().isEmpty()) {
        return false;
    }

    m_process.setProperty(
        "sshTimedOut", false);

    const QStringList args{
        QStringLiteral("-o"),
        QStringLiteral("BatchMode=yes"),

        QStringLiteral("-o"),
        QStringLiteral("ConnectTimeout=5"),

        QStringLiteral("-o"),
        QStringLiteral(
            "StrictHostKeyChecking=accept-new"),

        QStringLiteral("-p"),
        QString::number(m_port),

        m_user + QStringLiteral("@") + m_host,

        command
    };

    m_process.start(
        QStringLiteral("ssh.exe"),
        args);

    int timeoutMs =
        property("sshTimeoutMs").toInt();

    if (timeoutMs <= 0) {
        timeoutMs = 15000;
    }

    m_timeout.start(timeoutMs);

    return true;
}

// ============================================================
// Connection test
// ============================================================

void SshClient::checkConnection()
{
    execute(QStringLiteral("uname -a"));
}
