#include "devicemaintenancewindow.h"

#include "patchmanager.h"
#include "patchinstaller.h"
#include "patchzipimporter.h"
#include "sshclient.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

DeviceMaintenanceWindow::DeviceMaintenanceWindow(QWidget *parent)
    : QDialog(parent)
    , m_sshClient(new SshClient(this))
    , m_patchManager(new PatchManager(this))
    , m_patchInstaller(new PatchInstaller(this))
{
    setupUi();

    connect(m_checkButton, &QPushButton::clicked,
            this, &DeviceMaintenanceWindow::checkConnection);
    connect(m_sshClient, &SshClient::commandFinished,
            this, &DeviceMaintenanceWindow::onCommandFinished);
    connect(m_sshClient, &SshClient::connectionError,
            this, &DeviceMaintenanceWindow::onConnectionError);

    connect(m_refreshButton, &QPushButton::clicked,
            this, &DeviceMaintenanceWindow::refreshPatches);
    connect(m_patchTable, &QTableWidget::itemSelectionChanged,
            this, &DeviceMaintenanceWindow::updatePatchSelection);

    connect(m_installButton, &QPushButton::clicked,
            this, &DeviceMaintenanceWindow::installSelectedPatch);
    connect(m_uninstallButton, &QPushButton::clicked,
            this, &DeviceMaintenanceWindow::uninstallSelectedPatch);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &DeviceMaintenanceWindow::cancelPatchOperation);

    connect(m_patchInstaller, &PatchInstaller::progressChanged,
            this, &DeviceMaintenanceWindow::onPatchLog);
    connect(m_patchInstaller, &PatchInstaller::operationFinished,
            this, &DeviceMaintenanceWindow::onPatchFinished);
    connect(m_patchInstaller, &PatchInstaller::operationStarted,
            this, [this](const QString &patchId, const QString &operation) {
                appendLog(QStringLiteral("Started %1: %2").arg(operation, patchId));
            });

    refreshPatches();
    setPatchBusy(false);
}

DeviceMaintenanceWindow::~DeviceMaintenanceWindow() = default;

void DeviceMaintenanceWindow::setupUi()
{
    setWindowTitle(QStringLiteral("Device Maintenance"));
    resize(860, 680);
    setMinimumSize(680, 500);

    auto *mainLayout = new QVBoxLayout(this);

    auto *connectionGroup =
        new QGroupBox(QStringLiteral("SSH Connection"), this);
    auto *connectionLayout = new QFormLayout(connectionGroup);

    m_hostEdit = new QLineEdit(QStringLiteral("192.168.7.2"), connectionGroup);
    m_userEdit = new QLineEdit(QStringLiteral("root"), connectionGroup);
    m_portEdit = new QLineEdit(QStringLiteral("22"), connectionGroup);
    m_portEdit->setValidator(new QIntValidator(1, 65535, m_portEdit));

    connectionLayout->addRow(QStringLiteral("Host:"), m_hostEdit);
    connectionLayout->addRow(QStringLiteral("Username:"), m_userEdit);
    connectionLayout->addRow(QStringLiteral("SSH Port:"), m_portEdit);
    mainLayout->addWidget(connectionGroup);

    auto *connectionControls = new QHBoxLayout;
    m_checkButton = new QPushButton(QStringLiteral("Check Connection"), this);
    m_statusLabel = new QLabel(QStringLiteral("Not connected"), this);
    connectionControls->addWidget(m_checkButton);
    connectionControls->addWidget(m_statusLabel, 1);
    mainLayout->addLayout(connectionControls);

    auto *patchGroup = new QGroupBox(QStringLiteral("Available Patches"), this);
    auto *patchLayout = new QVBoxLayout(patchGroup);

    m_patchDirectoryLabel =
        new QLabel(m_patchManager->patchDirectory(), patchGroup);
    m_patchDirectoryLabel->setWordWrap(true);
    m_patchDirectoryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    patchLayout->addWidget(m_patchDirectoryLabel);

    auto *folderControls = new QHBoxLayout;
    auto *openFolderButton =
        new QPushButton(QStringLiteral("Open Patches Folder"), patchGroup);
    auto *importZipButton =
        new QPushButton(QStringLiteral("Import Patch ZIP..."), patchGroup);
    m_refreshButton =
        new QPushButton(QStringLiteral("Refresh Patches"), patchGroup);

    folderControls->addWidget(openFolderButton);
    folderControls->addWidget(importZipButton);
    folderControls->addWidget(m_refreshButton);
    folderControls->addStretch();
    patchLayout->addLayout(folderControls);

    connect(openFolderButton, &QPushButton::clicked, this, [this]() {
        const QString directory = m_patchManager->patchDirectory();
        if (!QDir().mkpath(directory)) {
            QMessageBox::warning(this, QStringLiteral("Folder Error"),
                                 QStringLiteral("Unable to create the patches folder:\n%1").arg(directory));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
            QMessageBox::warning(this, QStringLiteral("Folder Error"),
                                 QStringLiteral("Unable to open the patches folder:\n%1").arg(directory));
            return;
        }
        appendLog(QStringLiteral("Opened patches folder: %1").arg(directory));
    });

    // ZIP import is deliberately local-only: it never contacts the device.
    connect(importZipButton, &QPushButton::clicked, this,
            [this, importZipButton]() {
                if (m_patchBusy || m_connectionBusy || m_patchInstaller->isBusy())
                    return;

                const QString zipPath = QFileDialog::getOpenFileName(
                    this,
                    QStringLiteral("Import Patch ZIP"),
                    QString(),
                    QStringLiteral("ZIP Archives (*.zip)"));

                if (zipPath.isEmpty())
                    return;

                // Prevent re-entrant imports while PowerShell validates/extracts.
                importZipButton->setEnabled(false);

                QString importedDirectory;
                QString errorMessage;
                const bool ok = PatchZipImporter::importZip(
                    zipPath,
                    m_patchManager->patchDirectory(),
                    importedDirectory,
                    errorMessage);

                importZipButton->setEnabled(true);

                if (!ok) {
                    appendLog(QStringLiteral("ZIP import failed: %1").arg(errorMessage));
                    QMessageBox::warning(
                        this, QStringLiteral("Import Failed"),
                        errorMessage.isEmpty()
                            ? QStringLiteral("The ZIP archive could not be imported.")
                            : errorMessage);
                    return;
                }

                appendLog(QStringLiteral("Imported patch: %1").arg(importedDirectory));
                refreshPatches();
                QMessageBox::information(
                    this,
                    QStringLiteral("Patch Imported"),
                    QStringLiteral("Patch imported successfully:\n%1\n\n"
                                   "Importing does not install the patch on the device.")
                        .arg(importedDirectory));
            });

    m_patchTable = new QTableWidget(patchGroup);
    m_patchTable->setColumnCount(4);
    m_patchTable->setHorizontalHeaderLabels({
        QStringLiteral("Patch"),
        QStringLiteral("Version"),
        QStringLiteral("Description"),
        QStringLiteral("Status")
    });
    m_patchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_patchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_patchTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_patchTable->setAlternatingRowColors(true);
    m_patchTable->verticalHeader()->setVisible(false);
    m_patchTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_patchTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_patchTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    m_patchTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    patchLayout->addWidget(m_patchTable);

    auto *operationControls = new QHBoxLayout;
    m_installButton = new QPushButton(QStringLiteral("Install"), patchGroup);
    m_uninstallButton = new QPushButton(QStringLiteral("Uninstall"), patchGroup);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel Operation"), patchGroup);
    m_installButton->setMinimumWidth(110);
    m_uninstallButton->setMinimumWidth(110);
    m_cancelButton->setMinimumWidth(140);
    operationControls->addWidget(m_installButton);
    operationControls->addWidget(m_uninstallButton);
    operationControls->addStretch();
    operationControls->addWidget(m_cancelButton);
    patchLayout->addLayout(operationControls);
    mainLayout->addWidget(patchGroup, 2);

    auto *logGroup = new QGroupBox(QStringLiteral("Device Log"), this);
    auto *logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QPlainTextEdit(logGroup);
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText(
        QStringLiteral("SSH and patch management messages will appear here."));
    logLayout->addWidget(m_logEdit);
    mainLayout->addWidget(logGroup, 1);

    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->addStretch();
    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, [this]() {
        if (m_patchBusy || m_connectionBusy) {
            QMessageBox::information(
                this, QStringLiteral("Operation in Progress"),
                QStringLiteral("Wait for the current operation to finish "
                               "before closing this window."));
            return;
        }
        reject();
    });
    bottomLayout->addWidget(closeButton);
    mainLayout->addLayout(bottomLayout);
}

void DeviceMaintenanceWindow::appendLog(const QString &message)
{
    if (!message.isEmpty() && m_logEdit)
        m_logEdit->appendPlainText(message);
}

void DeviceMaintenanceWindow::onPatchLog(const QString &message)
{
    appendLog(message);
}

void DeviceMaintenanceWindow::setBusy(bool busy)
{
    m_connectionBusy = busy;
    const bool enabled = !m_connectionBusy && !m_patchBusy;
    m_checkButton->setEnabled(enabled);
    m_hostEdit->setEnabled(enabled);
    m_userEdit->setEnabled(enabled);
    m_portEdit->setEnabled(enabled);
    updatePatchSelection();
}

void DeviceMaintenanceWindow::setPatchBusy(bool busy)
{
    m_patchBusy = busy;
    const bool enabled = !m_connectionBusy && !m_patchBusy;
    m_checkButton->setEnabled(enabled);
    m_hostEdit->setEnabled(enabled);
    m_userEdit->setEnabled(enabled);
    m_portEdit->setEnabled(enabled);
    m_refreshButton->setEnabled(!busy);
    m_patchTable->setEnabled(!busy);
    m_cancelButton->setEnabled(busy);
    updatePatchSelection();
}

bool DeviceMaintenanceWindow::validateConnectionSettings(
    QString &host, QString &user, int &port) const
{
    host = m_hostEdit->text().trimmed();
    user = m_userEdit->text().trimmed();
    bool validPort = false;
    port = m_portEdit->text().trimmed().toInt(&validPort);
    return !host.isEmpty() && !user.isEmpty()
           && validPort && port >= 1 && port <= 65535;
}

void DeviceMaintenanceWindow::checkConnection()
{
    if (m_patchBusy || m_connectionBusy || m_sshClient->isBusy())
        return;

    QString host, user;
    int port = 0;
    if (!validateConnectionSettings(host, user, port)) {
        QMessageBox::warning(
            this, QStringLiteral("Invalid SSH Settings"),
            QStringLiteral("Enter a valid host, username and SSH port."));
        return;
    }

    m_sshClient->setHost(host);
    m_sshClient->setUser(user);
    m_sshClient->setPort(port);
    appendLog(QStringLiteral("Checking SSH: %1@%2:%3")
                  .arg(user, host).arg(port));
    m_statusLabel->setText(QStringLiteral("Connecting..."));
    setBusy(true);

    if (!m_sshClient->execute(QStringLiteral("uname -a"))) {
        appendLog(QStringLiteral("Unable to start SSH test."));
        m_statusLabel->setText(QStringLiteral("Connection test failed"));
        setBusy(false);
    }
}

void DeviceMaintenanceWindow::onCommandFinished(
    bool success, const QString &output, const QString &error)
{
    if (!output.trimmed().isEmpty())
        appendLog(output.trimmed());
    if (!error.trimmed().isEmpty())
        appendLog(error.trimmed());

    m_statusLabel->setText(
        success ? QStringLiteral("Connected")
                : QStringLiteral("Connection failed"));
    appendLog(success
                  ? QStringLiteral("SSH connection test successful.")
                  : QStringLiteral("SSH connection test failed."));
    setBusy(false);
}

void DeviceMaintenanceWindow::onConnectionError(const QString &message)
{
    appendLog(QStringLiteral("SSH error: %1").arg(message));
    m_statusLabel->setText(QStringLiteral("Connection error"));
    if (!m_sshClient->isBusy())
        setBusy(false);
}

void DeviceMaintenanceWindow::refreshPatches()
{
    if (m_patchBusy)
        return;

    m_patches = m_patchManager->scanPatches();
    m_patchTable->setRowCount(0);
    m_patchDirectoryLabel->setText(m_patchManager->patchDirectory());

    for (const PatchInfo &patch : m_patches) {
        const int row = m_patchTable->rowCount();
        m_patchTable->insertRow(row);

        const QString name = patch.name.isEmpty()
                                 ? QDir(patch.directory).dirName() : patch.name;

        auto *nameItem = new QTableWidgetItem(name);
        auto *versionItem = new QTableWidgetItem(patch.version);
        auto *descriptionItem = new QTableWidgetItem(patch.description);
        auto *statusItem = new QTableWidgetItem(
            patch.valid ? QStringLiteral("Ready")
                        : QStringLiteral("Invalid"));

        if (!patch.valid) {
            nameItem->setToolTip(patch.error);
            statusItem->setToolTip(patch.error);
        }
        m_patchTable->setItem(row, 0, nameItem);
        m_patchTable->setItem(row, 1, versionItem);
        m_patchTable->setItem(row, 2, descriptionItem);
        m_patchTable->setItem(row, 3, statusItem);
    }

    appendLog(QStringLiteral("Found %1 patch(es) in %2")
                  .arg(m_patches.size())
                  .arg(m_patchManager->patchDirectory()));
    for (const PatchInfo &patch : m_patches) {
        if (!patch.valid) {
            appendLog(QStringLiteral("Invalid patch [%1]: %2")
                          .arg(patch.id, patch.error));
        }
    }
    updatePatchSelection();
}

const PatchInfo *DeviceMaintenanceWindow::selectedPatch() const
{
    if (!m_patchTable)
        return nullptr;
    const int row = m_patchTable->currentRow();
    if (row < 0 || row >= m_patches.size())
        return nullptr;
    return &m_patches.at(row);
}

void DeviceMaintenanceWindow::updatePatchSelection()
{
    const PatchInfo *patch = selectedPatch();
    const bool canOperate =
        patch && patch->valid && !m_patchBusy && !m_connectionBusy;
    m_installButton->setEnabled(canOperate);
    m_uninstallButton->setEnabled(canOperate);
}

void DeviceMaintenanceWindow::installSelectedPatch()
{
    if (m_patchBusy || m_connectionBusy || m_patchInstaller->isBusy())
        return;

    const PatchInfo *patch = selectedPatch();
    if (!patch || !patch->valid) {
        QMessageBox::warning(this, QStringLiteral("Invalid Patch"),
                             QStringLiteral("Select a valid patch before installing."));
        return;
    }

    QString host, user;
    int port = 0;
    if (!validateConnectionSettings(host, user, port)) {
        QMessageBox::warning(this, QStringLiteral("Invalid SSH Settings"),
                             QStringLiteral("Enter a valid host, username and SSH port."));
        return;
    }

    const QString message = QStringLiteral(
                                "Install this patch on the remote device?\n\n"
                                "Patch: %1\nVersion: %2\nTarget: %3@%4:%5\n\n"
                                "The installation script may modify Linux system files "
                                "and startup configuration.\n\nContinue?")
                                .arg(patch->name).arg(patch->version)
                                .arg(user).arg(host).arg(port);

    if (QMessageBox::question(
            this, QStringLiteral("Confirm Patch Installation"),
            message, QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes)
        return;

    const PatchInfo selected = *patch;
    m_patchInstaller->setConnection(host, user, port);
    appendLog(QStringLiteral("========================================"));
    appendLog(QStringLiteral("Installing patch: %1").arg(selected.id));
    appendLog(QStringLiteral("Target: %1@%2:%3").arg(user, host).arg(port));
    setPatchBusy(true);

    if (!m_patchInstaller->install(selected)) {
        appendLog(QStringLiteral("Unable to start patch installation."));
        setPatchBusy(false);
        QMessageBox::warning(this, QStringLiteral("Installation Not Started"),
                             QStringLiteral("The patch installer could not start."));
    }
}

void DeviceMaintenanceWindow::uninstallSelectedPatch()
{
    if (m_patchBusy || m_connectionBusy || m_patchInstaller->isBusy())
        return;

    const PatchInfo *patch = selectedPatch();
    if (!patch || !patch->valid) {
        QMessageBox::warning(this, QStringLiteral("Invalid Patch"),
                             QStringLiteral("Select a valid patch before uninstalling."));
        return;
    }

    QString host, user;
    int port = 0;
    if (!validateConnectionSettings(host, user, port)) {
        QMessageBox::warning(this, QStringLiteral("Invalid SSH Settings"),
                             QStringLiteral("Enter a valid host, username and SSH port."));
        return;
    }

    const QString message = QStringLiteral(
                                "Uninstall this patch from the remote device?\n\n"
                                "Patch: %1\nVersion: %2\nTarget: %3@%4:%5\n\n"
                                "The uninstall script may remove Linux system files "
                                "or startup configuration.\n\nContinue?")
                                .arg(patch->name).arg(patch->version)
                                .arg(user).arg(host).arg(port);

    if (QMessageBox::question(
            this, QStringLiteral("Confirm Patch Uninstallation"),
            message, QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes)
        return;

    const PatchInfo selected = *patch;
    m_patchInstaller->setConnection(host, user, port);
    appendLog(QStringLiteral("========================================"));
    appendLog(QStringLiteral("Uninstalling patch: %1").arg(selected.id));
    appendLog(QStringLiteral("Target: %1@%2:%3").arg(user, host).arg(port));
    setPatchBusy(true);

    if (!m_patchInstaller->uninstall(selected)) {
        appendLog(QStringLiteral("Unable to start patch uninstallation."));
        setPatchBusy(false);
        QMessageBox::warning(this, QStringLiteral("Uninstallation Not Started"),
                             QStringLiteral("The patch uninstaller could not start."));
    }
}

void DeviceMaintenanceWindow::cancelPatchOperation()
{
    if (!m_patchBusy || !m_patchInstaller->isBusy())
        return;

    const auto answer = QMessageBox::warning(
        this, QStringLiteral("Cancel Patch Operation"),
        QStringLiteral(
            "Request cancellation of the current operation?\n\n"
            "Cancellation does not roll back changes already made "
            "on the remote Linux device.\n\n"
            "A remote installation script may continue running even "
            "after the SSH connection is interrupted."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    appendLog(QStringLiteral("Cancellation requested..."));
    m_cancelButton->setEnabled(false);
    m_patchInstaller->cancel();
}

void DeviceMaintenanceWindow::onPatchFinished(
    bool success, const QString &message)
{
    appendLog(QStringLiteral("----------------------------------------"));
    if (!message.trimmed().isEmpty())
        appendLog(message);
    appendLog(success
                  ? QStringLiteral("Patch operation completed successfully.")
                  : QStringLiteral("Patch operation did not complete successfully."));

    setPatchBusy(false);

    if (success) {
        QMessageBox::information(
            this, QStringLiteral("Patch Operation Completed"),
            message.isEmpty()
                ? QStringLiteral("The patch operation completed.\n\n"
                                 "A device reboot may be required.")
                : message + QStringLiteral("\n\nA device reboot may be required."));
    } else {
        QMessageBox::warning(
            this, QStringLiteral("Patch Operation Failed"),
            message.isEmpty()
                ? QStringLiteral("The patch operation failed or was cancelled.\n"
                                 "Check the device log for details.")
                : message);
    }
    updatePatchSelection();
}
