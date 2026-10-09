
#ifndef LUTREMOTEBROWSER_H
#define LUTREMOTEBROWSER_H

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class LutRemoteClient;

class LutRemoteBrowser : public QDialog
{
    Q_OBJECT

public:
    explicit LutRemoteBrowser(QWidget *parent = nullptr);
    ~LutRemoteBrowser() override = default;

    void setActiveLutName(const QString &fileName);

signals:
    void activeLutChanged(const QString &fileName);

private:
    void setupUi();
    void refreshFiles();
    void uploadBin();
    void confirmOverwrite(const QString &localPath);
    void updateSelection();
    void updateActiveLutMarker();

    void handleFilesReceived(const QStringList &fileNames);
    void handleUploadCompleted(const QString &remotePath);
    void handleOperationFailed(const QString &message);
    void handleBusyChanged(bool busy);

    QLabel *m_connectionLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_fileCountLabel = nullptr;

    QListWidget *m_fileList = nullptr;

    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    LutRemoteClient *m_client = nullptr;

    QStringList m_remoteFiles;
    QString m_selectedRemoteFile;
    QString m_activeLutName;
    bool m_busy = false;
};

#endif // LUTREMOTEBROWSER_H
