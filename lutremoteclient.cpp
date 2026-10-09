#include "lutremoteclient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

namespace {
const QString RemoteHost = "root@192.168.7.2";
const QString RemoteDirectory = "/mnt/lut";

QStringList sshOptions()
{
    return {"-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
            "-o", "StrictHostKeyChecking=accept-new"};
}

QStringList scpOptions()
{
    QStringList options = sshOptions();
    options.prepend("22");
    options.prepend("-P");
    return options;
}

QString shellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace("'", "'\\''");
    return "'" + escaped + "'";
}

bool safeFileName(const QString &name)
{
    static const QRegularExpression pattern("^[A-Za-z0-9][A-Za-z0-9._-]*\\.bin$",
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern.match(name).hasMatch();
}

QString cleanupCommand(const QString &path)
{
    return "rm -f -- " + shellQuote(path);
}
}

LutRemoteClient::LutRemoteClient(QObject *parent)
    : QObject(parent), m_process(new QProcess(this))
{
    connect(m_process, &QProcess::finished,
            this, &LutRemoteClient::handleProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &LutRemoteClient::handleProcessError);
}

LutRemoteClient::~LutRemoteClient()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    // A cancelled upload can leave a remote temporary file if the connection
    // is lost. Best-effort cleanup is handled during normal error paths.
}

bool LutRemoteClient::isBusy() const
{
    return m_operation != Operation::None;
}

void LutRemoteClient::startProcess(const QString &program,
                                   const QStringList &arguments,
                                   Operation operation)
{
    m_operation = operation;
    m_process->start(program, arguments);
}

void LutRemoteClient::refreshFiles()
{
    if (isBusy()) return;
    m_cancelRequested = false;
    emit busyChanged(true);
    emit statusChanged("Reading LUT files from ARM Linux...");
    QStringList arguments = sshOptions();
    arguments << RemoteHost << "LC_ALL=C ls -1Ap " + shellQuote(RemoteDirectory);
    startProcess("ssh.exe", arguments, Operation::ListFiles);
}

bool LutRemoteClient::validateLocalBin(const QString &filePath,
                                       QString &errorMessage) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        errorMessage = "Selected BIN file does not exist.";
        return false;
    }
    if (info.size() != BinarySize) {
        errorMessage = QString("Invalid BIN size.\nExpected: %1 bytes\nActual: %2 bytes")
        .arg(BinarySize).arg(info.size());
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QString("Cannot open BIN file: %1").arg(file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.size() != BinarySize) {
        errorMessage = "Failed to read the complete BIN file.";
        return false;
    }
    for (qsizetype i = 0; i < BinarySize; i += 4) {
        const quint32 value = static_cast<quint8>(data.at(i)) |
                              (static_cast<quint32>(static_cast<quint8>(data.at(i + 1))) << 8) |
                              (static_cast<quint32>(static_cast<quint8>(data.at(i + 2))) << 16) |
                              (static_cast<quint32>(static_cast<quint8>(data.at(i + 3))) << 24);
        if (value > 8191u) {
            errorMessage = QString("Invalid LUT value at index %1: %2")
            .arg(i / 4).arg(value);
            return false;
        }
    }
    return true;
}

void LutRemoteClient::uploadBin(const QString &localPath, bool overwrite)
{
    if (isBusy()) return;
    QString errorMessage;
    if (!validateLocalBin(localPath, errorMessage)) {
        emit operationFailed(errorMessage);
        return;
    }
    m_localPath = localPath;
    m_remoteFileName = QFileInfo(localPath).fileName();
    if (!safeFileName(m_remoteFileName)) {
        emit operationFailed("BIN filename must use ASCII letters, digits, '.', '_' or '-' and end in .bin.");
        return;
    }
    m_remotePath = RemoteDirectory + "/" + m_remoteFileName;
    m_remoteTemporaryPath = RemoteDirectory + "/.lut-upload-" +
                            QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp";
    m_allowOverwrite = overwrite;
    m_cancelRequested = false;
    emit busyChanged(true);
    emit statusChanged("Checking remote LUT destination...");
    const QString command =
        "if [ -d " + shellQuote(RemoteDirectory) + " ]; then "
                                                   "if [ -e " + shellQuote(m_remotePath) + " ]; then printf 'EXISTS\\n'; "
                                     "else printf 'MISSING\\n'; fi; "
                                     "else printf 'NO_DIRECTORY\\n'; fi";
    QStringList arguments = sshOptions();
    arguments << RemoteHost << command;
    startProcess("ssh.exe", arguments, Operation::CheckDestination);
}

void LutRemoteClient::cancel()
{
    if (!isBusy()) return;
    m_cancelRequested = true;
    if (m_process->state() != QProcess::NotRunning) m_process->kill();
}

void LutRemoteClient::handleProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart && isBusy())
        fail("Unable to start Windows OpenSSH process. Check ssh.exe and scp.exe.");
}

void LutRemoteClient::handleProcessFinished(int exitCode,
                                            QProcess::ExitStatus exitStatus)
{
    const Operation completedOperation = m_operation;
    if (completedOperation == Operation::None) return;
    const QByteArray output = m_process->readAllStandardOutput();
    const QString standardError =
        QString::fromUtf8(m_process->readAllStandardError()).trimmed();

    if (m_cancelRequested) {
        const QString temp = m_remoteTemporaryPath;
        const bool cleanup = completedOperation == Operation::UploadTemporary ||
                             completedOperation == Operation::VerifyAndCommit;
        finish();
        if (cleanup) {
            // Detached cleanup attempt: it must not modify the final BIN.
            auto *process = new QProcess(this);
            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    process, &QObject::deleteLater);
            QStringList args = sshOptions();
            args << RemoteHost << cleanupCommand(temp);
            process->start("ssh.exe", args);
        }
        emit statusChanged("Operation cancelled; remote temporary cleanup requested.");
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        fail(QString("Remote operation failed.\n%1")
                 .arg(standardError.isEmpty()
                          ? QString("Process exit code: %1").arg(exitCode)
                          : standardError));
        return;
    }
    switch (completedOperation) {
    case Operation::ListFiles: handleFileList(output); break;
    case Operation::CheckDestination: handleDestinationCheck(output); break;
    case Operation::UploadTemporary: handleUploadFinished(); break;
    case Operation::VerifyAndCommit: handleCommitFinished(); break;
    case Operation::None: break;
    }
}

void LutRemoteClient::handleFileList(const QByteArray &output)
{
    QStringList fileNames;
    const QStringList lines = QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
    for (QString name : lines) {
        name = name.trimmed();
        if (name.endsWith('/') || !name.endsWith(".bin", Qt::CaseInsensitive)) continue;
        fileNames.append(name);
    }
    fileNames.sort(Qt::CaseInsensitive);
    finish();
    emit filesReceived(fileNames);
    emit statusChanged(QString("%1 LUT file(s) found on ARM Linux.").arg(fileNames.size()));
}

void LutRemoteClient::handleDestinationCheck(const QByteArray &output)
{
    const QString result = QString::fromUtf8(output).trimmed();
    if (result == "NO_DIRECTORY") {
        fail("Remote LUT directory does not exist: " + RemoteDirectory);
        return;
    }
    if (result != "EXISTS" && result != "MISSING") {
        fail("Unexpected response while checking remote LUT.");
        return;
    }
    if (result == "EXISTS" && !m_allowOverwrite) {
        fail(QString("Remote file already exists:\n%1\n\nOverwrite confirmation is required.")
                 .arg(m_remotePath));
        return;
    }
    emit statusChanged(QString("Uploading %1 to ARM Linux...").arg(m_remoteFileName));
    QStringList arguments = scpOptions();
    arguments << m_localPath << RemoteHost + ":" + m_remoteTemporaryPath;
    startProcess("scp.exe", arguments, Operation::UploadTemporary);
}

void LutRemoteClient::handleUploadFinished()
{
    emit statusChanged("Checking SHA-256 and committing uploaded BIN...");
    QFile localFile(m_localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        fail("Cannot reopen local BIN for SHA-256 verification.");
        return;
    }
    const QByteArray localData = localFile.readAll();
    if (localData.size() != BinarySize) {
        fail("Local BIN changed size during upload.");
        return;
    }
    const QString localHash = QString::fromLatin1(
        QCryptographicHash::hash(localData, QCryptographicHash::Sha256).toHex());
    const QString temp = shellQuote(m_remoteTemporaryPath);
    const QString dest = shellQuote(m_remotePath);
    // ARM Linux provides OpenSSL but not sha256sum. The -r format prints
    // the digest first; shell parameter expansion extracts that first field.
    QString command =
        "set -e; "
        "test -f " + temp + "; "
                 "test \"$(wc -c < " + temp + ")\" -eq 16384; "
                 "actual=$(openssl dgst -sha256 -r " + temp + "); "
                 "actual=${actual%% *}; "
                 "test \"$actual\" = " + shellQuote(localHash) + "; ";
    if (m_allowOverwrite) {
        command += "mv -f -- " + temp + " " + dest;
    } else {
        // A hard link creates the destination only when absent. Since temp and
        // final reside in the same directory, the operation is atomic.
        command += "ln -- " + temp + " " + dest + "; rm -f -- " + temp;
    }
    QStringList arguments = sshOptions();
    arguments << RemoteHost << command;
    startProcess("ssh.exe", arguments, Operation::VerifyAndCommit);
}

void LutRemoteClient::handleCommitFinished()
{
    const QString completedPath = m_remotePath;
    finish();
    emit uploadCompleted(completedPath);
    emit statusChanged(QString("SHA-256 verified; LUT uploaded successfully: %1")
                           .arg(completedPath));
}

void LutRemoteClient::fail(const QString &message)
{
    const Operation failedOperation = m_operation;
    const QString temp = m_remoteTemporaryPath;
    finish();
    if (failedOperation == Operation::UploadTemporary ||
        failedOperation == Operation::VerifyAndCommit) {
        // Best-effort cleanup without touching any existing final BIN.
        auto *process = new QProcess(this);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                process, &QObject::deleteLater);
        QStringList args = sshOptions();
        args << RemoteHost << cleanupCommand(temp);
        process->start("ssh.exe", args);
    }
    emit operationFailed(message);
    emit statusChanged("LUT operation failed.");
}

void LutRemoteClient::finish()
{
    m_operation = Operation::None;
    m_cancelRequested = false;
    emit busyChanged(false);
}
