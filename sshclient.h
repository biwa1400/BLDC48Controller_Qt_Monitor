
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class SshClient : public QObject
{
    Q_OBJECT

public:
    explicit SshClient(QObject *parent = nullptr);
    ~SshClient() override;

    // SSH connection settings
    void setHost(const QString &host);
    void setUser(const QString &user);
    void setPort(int port);

    // SSH command timeout in milliseconds.
    //
    // Default: 15000 ms
    //
    // Examples:
    //   setTimeout(15000);   // Normal commands
    //   setTimeout(30000);   // SHA-256 verification
    //   setTimeout(120000);  // Patch installation
    //
    // Applies to subsequent commands.
    void setTimeout(int milliseconds);

    // SSH operations
    bool isBusy() const;
    bool execute(const QString &command);
    void checkConnection();

signals:
    void commandFinished(
        bool success,
        const QString &output,
        const QString &error);

    void connectionError(
        const QString &message);

private:
    QProcess m_process;
    QTimer m_timeout;

    QString m_host = QStringLiteral("192.168.7.2");
    QString m_user = QStringLiteral("root");

    int m_port = 22;
};
