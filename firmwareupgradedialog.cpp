#include "firmwareupgradedialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const QString RemoteHost = QStringLiteral("root@192.168.7.2");
const QString RemoteTemporary = QStringLiteral("/home/root/start_firmware.tmp");
const QString RemoteDestination = QStringLiteral("/home/root/start_firmware");

QStringList sshOptions()
{
    return {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
            QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5"),
            QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new")};
}

QStringList scpOptions()
{
    QStringList args = sshOptions();
    args.prepend(QStringLiteral("22"));
    args.prepend(QStringLiteral("-P"));
    return args;
}
} // namespace

FirmwareUpgradeDialog::FirmwareUpgradeDialog(QWidget *parent)
    : QDialog(parent), m_process(new QProcess(this))
{
    setWindowTitle(tr("Upgrade Motor Firmware"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
        tr("Upload an ARM Linux executable to %1. The running firmware will not be restarted.")
            .arg(RemoteDestination), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *fileRow = new QHBoxLayout;
    m_filePath = new QLineEdit(this);
    m_filePath->setPlaceholderText(tr("Choose a local ARM executable..."));
    m_filePath->setReadOnly(true);
    m_browseButton = new QPushButton(tr("Browse..."), this);
    fileRow->addWidget(m_filePath, 1);
    fileRow->addWidget(m_browseButton);
    layout->addLayout(fileRow);

    auto *target = new QLabel(tr("Target: %1 (%2)").arg(RemoteHost, RemoteDestination), this);
    layout->addWidget(target);

    m_statusLabel = new QLabel(tr("Select a firmware file to begin."), this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_uploadButton = new QPushButton(tr("Upload Firmware"), this);
    auto *closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_uploadButton);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(m_browseButton, &QPushButton::clicked, this, [this] { selectFile(); });
    connect(m_uploadButton, &QPushButton::clicked, this, [this] { startUpload(); });
    connect(closeButton, &QPushButton::clicked, this, [this] {
        if (m_stage != Stage::Idle) {
            QMessageBox::information(this, tr("Upload in progress"),
                                     tr("Wait for the firmware upload to finish before closing."));
            return;
        }
        close();
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FirmwareUpgradeDialog::finishProcess);
    connect(m_process, &QProcess::errorOccurred,
            this, &FirmwareUpgradeDialog::handleProcessError);
    setBusy(false);
}

FirmwareUpgradeDialog::~FirmwareUpgradeDialog()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void FirmwareUpgradeDialog::selectFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select ARM Firmware Executable"), QString(), tr("All files (*)"));
    if (path.isEmpty()) return;
    m_localFile = path;
    m_filePath->setText(path);
    m_statusLabel->setText(tr("Ready to upload."));
    setBusy(false);
}

void FirmwareUpgradeDialog::startUpload()
{
    if (m_stage != Stage::Idle) return;
    const QFileInfo info(m_localFile);
    if (!info.exists() || !info.isFile() || !info.isReadable() || info.size() <= 0) {
        showFailure(tr("Choose an existing, readable, non-empty firmware file."));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Confirm Firmware Upload"),
        tr("Upload %1 to %2?\n\nThe existing firmware file will be replaced after upload. "
           "The running ARM program will not be restarted.")
            .arg(info.fileName(), RemoteDestination));
    if (answer != QMessageBox::Yes) return;

    m_stage = Stage::Uploading;
    setBusy(true);
    m_statusLabel->setText(tr("Uploading firmware to ARM Linux..."));
    QStringList args = scpOptions();
    args << m_localFile << (RemoteHost + QStringLiteral(":") + RemoteTemporary);
    m_process->start(QStringLiteral("scp.exe"), args);
}

void FirmwareUpgradeDialog::startInstall()
{
    m_stage = Stage::Installing;
    m_statusLabel->setText(tr("Setting executable permissions and installing firmware..."));
    // chmod before rename keeps the existing target intact if chmod fails.
    const QString command = QStringLiteral(
        "set -e; test -s /home/root/start_firmware.tmp; "
        "chmod 755 /home/root/start_firmware.tmp; "
        "mv -f /home/root/start_firmware.tmp /home/root/start_firmware; "
        "test -x /home/root/start_firmware");
    QStringList args = sshOptions();
    args << RemoteHost << command;
    m_process->start(QStringLiteral("ssh.exe"), args);
}

void FirmwareUpgradeDialog::finishProcess(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_stage == Stage::Idle) return;
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString detail = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
        showFailure(tr("Firmware operation failed (exit code %1).%2")
                        .arg(exitCode)
                        .arg(detail.isEmpty() ? QString() : QStringLiteral("\n") + detail));
        return;
    }
    if (m_stage == Stage::Uploading) {
        startInstall();
        return;
    }
    m_stage = Stage::Idle;
    setBusy(false);
    m_statusLabel->setText(tr("Firmware uploaded successfully: %1").arg(RemoteDestination));
    QMessageBox::information(this, tr("Upload Complete"),
                             tr("Firmware installed at %1 with executable permissions.\n"
                                "The running ARM program was not restarted.")
                                 .arg(RemoteDestination));
}

void FirmwareUpgradeDialog::handleProcessError(QProcess::ProcessError error)
{
    if (m_stage == Stage::Idle || error != QProcess::FailedToStart) return;
    showFailure(tr("Unable to start SSH/SCP. Check that Windows OpenSSH is installed and "
                   "ssh.exe / scp.exe are available in PATH."));
}

void FirmwareUpgradeDialog::setBusy(bool busy)
{
    m_browseButton->setEnabled(!busy);
    m_uploadButton->setEnabled(!busy && !m_localFile.isEmpty());
    m_progressBar->setRange(0, busy ? 0 : 1);
    if (!busy) m_progressBar->setValue(0);
}

void FirmwareUpgradeDialog::showFailure(const QString &message)
{
    m_stage = Stage::Idle;
    setBusy(false);
    m_statusLabel->setText(message);
    QMessageBox::warning(this, tr("Firmware Upload Failed"), message);
}
