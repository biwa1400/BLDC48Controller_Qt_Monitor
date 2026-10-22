#ifndef FIRMWAREUPGRADEDIALOG_H
#define FIRMWAREUPGRADEDIALOG_H

#include <QDialog>
#include <QProcess>
#include <QString>

class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;

// Uploads a selected ARM Linux executable to /home/root/start_firmware.
// Does not stop the motor process or disconnect the live data stream.
class FirmwareUpgradeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FirmwareUpgradeDialog(QWidget *parent = nullptr);
    ~FirmwareUpgradeDialog() override;

private:
    enum class Stage {
        Idle,
        Uploading,
        Installing
    };

    void selectFile();
    void startUpload();
    void startInstall();
    void finishProcess(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void setBusy(bool busy);
    void showFailure(const QString &message);

    QLineEdit *m_filePath = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QProcess *m_process = nullptr;
    Stage m_stage = Stage::Idle;
    QString m_localFile;
};

#endif // FIRMWAREUPGRADEDIALOG_H
