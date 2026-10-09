
#ifndef LUTREMOTECLIENT_H
#define LUTREMOTECLIENT_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QByteArray>

class LutRemoteClient : public QObject
{
    Q_OBJECT

public:
    explicit LutRemoteClient(QObject *parent = nullptr);
    ~LutRemoteClient() override;

    static constexpr qint64 BinarySize = 16384;

    bool isBusy() const;
    void refreshFiles();
    void uploadBin(const QString &localPath, bool overwrite);
    void cancel();

signals:
    void filesReceived(const QStringList &fileNames);
    void uploadCompleted(const QString &remotePath);
    void operationFailed(const QString &message);
    void statusChanged(const QString &message);
    void busyChanged(bool busy);

private:
    enum class Operation
    {
        None,
        ListFiles,
        CheckDestination,
        UploadTemporary,
        VerifyAndCommit
    };

    void startProcess(const QString &program,
                      const QStringList &arguments,
                      Operation operation);

    void handleProcessFinished(int exitCode,
                               QProcess::ExitStatus exitStatus);

    void handleProcessError(QProcess::ProcessError error);

    void handleFileList(const QByteArray &output);
    void handleDestinationCheck(const QByteArray &output);
    void handleUploadFinished();
    void handleCommitFinished();

    bool validateLocalBin(const QString &filePath,
                          QString &errorMessage) const;

    void fail(const QString &message);
    void finish();

    QProcess *m_process = nullptr;
    Operation m_operation = Operation::None;

    QString m_localPath;
    QString m_remoteFileName;
    QString m_remotePath;
    QString m_remoteTemporaryPath;

    bool m_allowOverwrite = false;
    bool m_cancelRequested = false;
};

#endif // LUTREMOTECLIENT_H
