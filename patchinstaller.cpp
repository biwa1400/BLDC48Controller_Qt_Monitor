
#include "patchinstaller.h"
#include "filetransferclient.h"
#include "sshclient.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFileInfoList>

namespace {

QString shellQuote(const QString &value)
{
    QString result = value;
    result.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'") + result + QStringLiteral("'");
}

bool safeName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));

    return pattern.match(name).hasMatch() &&
           name != QStringLiteral(".") &&
           name != QStringLiteral("..");
}

QString fileHash(const QString &path, QCryptographicHash::Algorithm algorithm)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(algorithm);

    while (!file.atEnd()) {
        const QByteArray data = file.read(65536);

        if (data.isEmpty() && file.error() != QFile::NoError)
            return {};

        hash.addData(data);
    }

    return QString::fromLatin1(hash.result().toHex());
}

}

PatchInstaller::PatchInstaller(QObject *parent)
    : QObject(parent)
    , m_sshClient(new SshClient(this))
    , m_transferClient(new FileTransferClient(this))
{
    connect(m_sshClient, &SshClient::commandFinished,
            this, &PatchInstaller::handleCommandFinished);

    connect(m_sshClient, &SshClient::connectionError,
            this, [this](const QString &message) {
                if (m_operation != Operation::None)
                    emit progressChanged(message);
            });

    connect(m_transferClient, &FileTransferClient::transferFinished,
            this, &PatchInstaller::handleTransferFinished);

    connect(m_transferClient, &FileTransferClient::transferError,
            this, [this](const QString &message) {
                if (m_operation != Operation::None)
                    emit progressChanged(message);
            });
}

PatchInstaller::~PatchInstaller() = default;

void PatchInstaller::setConnection(const QString &host,
                                   const QString &user,
                                   int port)
{
    if (isBusy())
        return;

    m_host = host.trimmed();
    m_user = user.trimmed();
    m_port = port;

    m_sshClient->setHost(m_host);
    m_sshClient->setUser(m_user);
    m_sshClient->setPort(m_port);

    m_transferClient->setHost(m_host);
    m_transferClient->setUser(m_user);
    m_transferClient->setPort(m_port);
}

bool PatchInstaller::isBusy() const
{
    return m_operation != Operation::None ||
           m_sshClient->isBusy() ||
           m_transferClient->isBusy();
}

bool PatchInstaller::install(const PatchInfo &patch)
{
    if (isBusy() || !patch.valid || !safeName(patch.id))
        return false;

    startOperation(patch, Operation::Install);
    return true;
}

bool PatchInstaller::uninstall(const PatchInfo &patch)
{
    if (isBusy() || !patch.valid || !safeName(patch.id))
        return false;

    startOperation(patch, Operation::Uninstall);
    return true;
}

void PatchInstaller::startOperation(const PatchInfo &patch,
                                    Operation operation)
{
    m_currentPatch = patch;
    m_operation = operation;
    m_stage = Stage::CheckingDevice;
    m_remoteDirectory.clear();
    m_filesToUpload.clear();
    m_uploadIndex = 0;

    const QDir directory(patch.directory);

    if (!directory.exists()) {
        finishOperation(false, QStringLiteral("Patch directory not found."));
        return;
    }

    // Reject nested content and symbolic links: this installer transfers
    // only ordinary top-level files and must not silently omit payloads.
    const QFileInfoList allEntries = directory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const QFileInfo &entry : allEntries) {
        if (!entry.isFile() || entry.isSymLink() || !safeName(entry.fileName())) {
            finishOperation(false,
                            QStringLiteral("Unsupported patch entry: %1").arg(entry.fileName()));
            return;
        }
    }

    const QFileInfoList files = directory.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDir::Name);

    for (const QFileInfo &file : files) {
        if (!safeName(file.fileName())) {
            finishOperation(false,
                            QStringLiteral("Unsafe patch filename: %1")
                                .arg(file.fileName()));
            return;
        }

        m_filesToUpload.append(file.absoluteFilePath());
    }

    if (m_filesToUpload.isEmpty()) {
        finishOperation(false, QStringLiteral("Patch directory is empty."));
        return;
    }

    const QString script = operation == Operation::Install
                               ? patch.installScript
                               : patch.uninstallScript;

    const QFileInfo scriptInfo(script);
    if (!scriptInfo.isFile() || scriptInfo.isSymLink() ||
        !safeName(scriptInfo.fileName()) ||
        scriptInfo.absolutePath() != directory.absolutePath()) {
        finishOperation(false,
                        QStringLiteral("Patch script must be an ordinary file inside the patch directory."));
        return;
    }

    emit operationStarted(
        patch.id,
        operation == Operation::Install
            ? QStringLiteral("Install")
            : QStringLiteral("Uninstall"));

    emit progressChanged(QStringLiteral("Checking Linux device..."));

    executeNextStage();
}

void PatchInstaller::executeNextStage()
{
    if (m_operation == Operation::None)
        return;

    QString command;

    switch (m_stage) {
    case Stage::CheckingDevice:
        command = QStringLiteral(
            "test \"$(id -u)\" = 0 && "
            "command -v sh >/dev/null 2>&1 && "
            "(command -v sha256sum >/dev/null 2>&1 || "
            "busybox md5sum /dev/null >/dev/null 2>&1) && "
            "command -v mktemp >/dev/null 2>&1");

        if (m_currentPatch.id == QStringLiteral("rndis-reconnect")) {
            command += QStringLiteral(
                " && test -d /etc/init.d"
                " && test -d /etc/rc5.d"
                " && test -w /etc/init.d"
                " && test -w /etc/rc5.d"
                " && found=0"
                " && for udc in /sys/class/udc/*; do"
                " test -w \"$udc/soft_connect\" && found=1;"
                " done"
                " && test \"$found\" = 1");
        }
        break;

    case Stage::PreparingDirectory:
        command = QStringLiteral(
            "mktemp -d /tmp/qt-patch-XXXXXXXX");
        break;

    case Stage::UploadingFiles: {
        if (m_uploadIndex >= m_filesToUpload.size()) {
            m_stage = Stage::VerifyingFiles;
            executeNextStage();
            return;
        }

        const QString localPath = m_filesToUpload.at(m_uploadIndex);
        const QString remotePath =
            m_remoteDirectory + QLatin1Char('/') +
            QFileInfo(localPath).fileName();

        emit progressChanged(
            QStringLiteral("Uploading %1 (%2/%3)")
                .arg(QFileInfo(localPath).fileName())
                .arg(m_uploadIndex + 1)
                .arg(m_filesToUpload.size()));

        if (!m_transferClient->uploadFile(localPath, remotePath)) {
            finishOperation(false,
                            QStringLiteral("Unable to start file upload."));
        }
        return;
    }

    case Stage::VerifyingFiles: {
        QStringList checks;

        for (const QString &path : m_filesToUpload) {
            // Prefer SHA-256 when available on the target. For minimal
            // BusyBox systems without SHA-256, verify using BusyBox MD5.
            // MD5 detects accidental corruption, not malicious tampering.
            const QString sha256 = fileHash(path, QCryptographicHash::Sha256);
            const QString md5 = fileHash(path, QCryptographicHash::Md5);

            if (sha256.isEmpty() || md5.isEmpty()) {
                finishOperation(false,
                                QStringLiteral("Unable to hash local file: %1")
                                    .arg(path));
                return;
            }

            const QString remotePath =
                m_remoteDirectory + QLatin1Char('/') +
                QFileInfo(path).fileName();

            // Remote shell compares against the hash for the algorithm
            // actually available. A nonzero result blocks script execution.
            checks.append(
                QStringLiteral(
                    "if command -v sha256sum >/dev/null 2>&1; then "
                    "test \"$(sha256sum %1 | cut -d ' ' -f 1)\" = %2; "
                    "else "
                    "test \"$(busybox md5sum %1 | cut -d ' ' -f 1)\" = %3; "
                    "fi")
                    .arg(shellQuote(remotePath),
                         shellQuote(sha256),
                         shellQuote(md5)));
        }

        const QString scriptPath =
            m_operation == Operation::Install
                ? m_currentPatch.installScript
                : m_currentPatch.uninstallScript;

        const QString remoteScript =
            m_remoteDirectory + QLatin1Char('/') +
            QFileInfo(scriptPath).fileName();

        checks.append(
            QStringLiteral("sh -n %1").arg(shellQuote(remoteScript)));

        command = checks.join(QStringLiteral(" && "));
        break;
    }

    case Stage::ExecutingScript: {
        const QString scriptPath =
            m_operation == Operation::Install
                ? m_currentPatch.installScript
                : m_currentPatch.uninstallScript;

        const QString remoteScript =
            m_remoteDirectory + QLatin1Char('/') +
            QFileInfo(scriptPath).fileName();

        command = QStringLiteral("cd %1 && sh %2")
                      .arg(shellQuote(m_remoteDirectory),
                           shellQuote(QFileInfo(remoteScript).fileName()));
        break;
    }

    case Stage::CheckingResult:
        if (m_currentPatch.id == QStringLiteral("rndis-reconnect")) {
            if (m_operation == Operation::Install) {
                command = QStringLiteral(
                    "test -f /etc/init.d/usb-rndis-reconnect"
                    " && test -L /etc/rc5.d/S99zUsbReconnect"
                    " && sh -n /etc/init.d/usb-rndis-reconnect");
            } else {
                command = QStringLiteral(
                    "test ! -e /etc/rc5.d/S99zUsbReconnect"
                    " && test ! -L /etc/rc5.d/S99zUsbReconnect");
            }
        } else {
            command = QStringLiteral("true");
        }
        break;

    case Stage::Idle:
        return;
    }

    // Configure the timeout for the command about to be started.
    // These are local SSH wait limits, not guaranteed remote execution limits.
    int timeoutMs = 15000;
    switch (m_stage) {
    case Stage::VerifyingFiles:
        timeoutMs = 30000;
        break;
    case Stage::ExecutingScript:
        timeoutMs = 120000;
        emit progressChanged(QStringLiteral(
            "Running remote script (SSH timeout: 120 seconds). "
            "If SSH times out, remote changes may still be in progress; do not retry automatically."));
        break;
    case Stage::CheckingDevice:
    case Stage::PreparingDirectory:
    case Stage::CheckingResult:
        timeoutMs = 15000;
        break;
    case Stage::UploadingFiles:
    case Stage::Idle:
        return;
    }

    m_sshClient->setTimeout(timeoutMs);
    if (!m_sshClient->execute(command)) {
        finishOperation(false,
                        QStringLiteral("Unable to start SSH command."));
    }
}

void PatchInstaller::handleCommandFinished(
    bool success,
    const QString &output,
    const QString &error)
{
    if (m_operation == Operation::None)
        return;

    // Idle while an operation exists means cancellation was requested.
    // Wait until the in-flight SSH process actually finishes before
    // resetting state; otherwise its late callback can corrupt a new job.
    if (m_stage == Stage::Idle) {
        finishOperation(false, QStringLiteral("Operation cancelled."));
        return;
    }

    if (!output.trimmed().isEmpty())
        emit progressChanged(output.trimmed());

    if (!error.trimmed().isEmpty())
        emit progressChanged(error.trimmed());

    if (!success) {
        finishOperation(false,
                        QStringLiteral("SSH command failed during stage %1.")
                            .arg(static_cast<int>(m_stage)));
        return;
    }

    switch (m_stage) {
    case Stage::CheckingDevice:
        emit progressChanged(QStringLiteral("Device check passed."));
        m_stage = Stage::PreparingDirectory;
        break;

    case Stage::PreparingDirectory:
        m_remoteDirectory = output.trimmed();

        if (!m_remoteDirectory.startsWith(
                QStringLiteral("/tmp/qt-patch-")) ||
            m_remoteDirectory.contains(QLatin1Char('\n')) ||
            m_remoteDirectory.contains(QLatin1Char('\r'))) {
            finishOperation(false,
                            QStringLiteral("Invalid remote staging directory."));
            return;
        }

        emit progressChanged(
            QStringLiteral("Staging directory: %1")
                .arg(m_remoteDirectory));

        m_stage = Stage::UploadingFiles;
        break;

    case Stage::VerifyingFiles:
        emit progressChanged(
            QStringLiteral("File hash (SHA-256 or BusyBox MD5) and shell syntax checks passed."));
        m_stage = Stage::ExecutingScript;
        break;

    case Stage::ExecutingScript:
        emit progressChanged(
            QStringLiteral("Script completed with exit code 0."));
        m_stage = Stage::CheckingResult;
        break;

    case Stage::CheckingResult:
        finishOperation(true,
                        QStringLiteral("Patch operation completed successfully."));
        return;

    default:
        finishOperation(false,
                        QStringLiteral("Unexpected installer state."));
        return;
    }

    executeNextStage();
}

void PatchInstaller::handleTransferFinished(
    bool success,
    const QString &output,
    const QString &error)
{
    if (m_operation == Operation::None)
        return;

    if (m_stage == Stage::Idle) {
        finishOperation(false, QStringLiteral("Operation cancelled."));
        return;
    }

    if (m_stage != Stage::UploadingFiles)
        return;

    if (!output.trimmed().isEmpty())
        emit progressChanged(output.trimmed());

    if (!error.trimmed().isEmpty())
        emit progressChanged(error.trimmed());

    if (!success) {
        finishOperation(false, QStringLiteral("File upload failed."));
        return;
    }

    ++m_uploadIndex;
    executeNextStage();
}

void PatchInstaller::finishOperation(
    bool success,
    const QString &message)
{
    if (m_operation == Operation::None)
        return;

    m_operation = Operation::None;
    m_stage = Stage::Idle;
    m_filesToUpload.clear();
    m_uploadIndex = 0;

    emit progressChanged(message);
    emit operationFinished(success, message);
}

void PatchInstaller::cancel()
{
    if (m_operation == Operation::None)
        return;

    if (m_stage == Stage::ExecutingScript ||
        m_stage == Stage::CheckingResult) {
        emit progressChanged(
            QStringLiteral(
                "Cannot safely cancel an operation after script execution has started."));
        return;
    }

    // Mark cancellation before asking the process to stop, so any
    // synchronous or delayed completion callback sees the cancelled state.
    m_stage = Stage::Idle;

    if (m_transferClient->isBusy()) {
        // Do not kill QProcess here: a queued finished() callback may arrive
        // after isBusy() turns false. Let this transfer finish, then stop.
        emit progressChanged(QStringLiteral(
            "Cancellation requested; waiting for current file transfer to finish..."));
        return;
    }

    if (m_sshClient->isBusy()) {
        emit progressChanged(QStringLiteral(
            "Cancellation requested; waiting for the current SSH check to finish..."));
        return;
    }

    finishOperation(false, QStringLiteral("Operation cancelled."));
}
