
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class FileTransferClient : public QObject
{
    Q_OBJECT

public:
    explicit FileTransferClient(QObject *parent = nullptr);
    ~FileTransferClient() override;

    void setHost(const QString &host);
    void setUser(const QString &user);
    void setPort(int port);
    void setTimeout(int milliseconds);

    bool isBusy() const;

    bool uploadFile(const QString &localPath,
                    const QString &remotePath);

    bool downloadFile(const QString &remotePath,
                      const QString &localPath);

    void cancel();

signals:
    void transferStarted(const QString &source,
                         const QString &destination);

    void transferFinished(bool success,
                          const QString &output,
                          const QString &error);

    void transferError(const QString &message);

private:
    bool startTransfer(const QString &source,
                       const QString &destination);

    QProcess m_process;
    QTimer m_timeout;

    QString m_host = QStringLiteral("192.168.7.2");
    QString m_user = QStringLiteral("root");

    int m_port = 22;
    int m_timeoutMs = 60000;
};
