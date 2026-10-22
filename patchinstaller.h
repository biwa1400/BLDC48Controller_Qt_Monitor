
#pragma once

#include "patchmanager.h"

#include <QObject>
#include <QString>

class SshClient;
class FileTransferClient;

class PatchInstaller : public QObject
{
    Q_OBJECT

public:
    explicit PatchInstaller(QObject *parent = nullptr);
    ~PatchInstaller() override;

    void setConnection(const QString &host,
                       const QString &user,
                       int port = 22);

    bool isBusy() const;

    bool install(const PatchInfo &patch);
    bool uninstall(const PatchInfo &patch);

    void cancel();

signals:
    void operationStarted(const QString &patchId,
                          const QString &operation);

    void progressChanged(const QString &message);

    void operationFinished(bool success,
                           const QString &message);

private:
    enum class Operation {
        None,
        Install,
        Uninstall
    };

    enum class Stage {
        Idle,
        CheckingDevice,
        PreparingDirectory,
        UploadingFiles,
        VerifyingFiles,
        ExecutingScript,
        CheckingResult
    };

    void startOperation(const PatchInfo &patch,
                        Operation operation);

    void executeNextStage();

    void handleCommandFinished(bool success,
                               const QString &output,
                               const QString &error);

    void handleTransferFinished(bool success,
                                const QString &output,
                                const QString &error);

    void finishOperation(bool success,
                         const QString &message);

    SshClient *m_sshClient = nullptr;
    FileTransferClient *m_transferClient = nullptr;

    QString m_host = QStringLiteral("192.168.7.2");
    QString m_user = QStringLiteral("root");
    int m_port = 22;

    PatchInfo m_currentPatch;
    Operation m_operation = Operation::None;
    Stage m_stage = Stage::Idle;

    QString m_remoteDirectory;
    QStringList m_filesToUpload;
    int m_uploadIndex = 0;
};
