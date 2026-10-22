
#pragma once

#include <QDialog>
#include <QString>
#include <QList>

#include "patchmanager.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QTableWidget;

class SshClient;
class PatchManager;
class PatchInstaller;

class DeviceMaintenanceWindow : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceMaintenanceWindow(QWidget *parent = nullptr);
    ~DeviceMaintenanceWindow() override;

private slots:
    // =========================================================
    // SSH connection
    // =========================================================

    void checkConnection();

    void onCommandFinished(
        bool success,
        const QString &output,
        const QString &error);

    void onConnectionError(
        const QString &message);

    // =========================================================
    // Patch management
    // =========================================================

    void refreshPatches();
    void updatePatchSelection();

    void installSelectedPatch();
    void uninstallSelectedPatch();
    void cancelPatchOperation();

    // =========================================================
    // Patch installer callbacks
    // =========================================================

    void onPatchLog(
        const QString &message);

    void onPatchFinished(
        bool success,
        const QString &message);

private:
    // =========================================================
    // UI
    // =========================================================

    void setupUi();

    void appendLog(
        const QString &message);

    void setBusy(
        bool busy);

    void setPatchBusy(
        bool busy);

    // =========================================================
    // Patch selection and validation
    // =========================================================

    const PatchInfo *selectedPatch() const;

    bool validateConnectionSettings(
        QString &host,
        QString &user,
        int &port) const;

    // =========================================================
    // SSH / Patch backend
    // =========================================================

    SshClient *m_sshClient = nullptr;
    PatchManager *m_patchManager = nullptr;
    PatchInstaller *m_patchInstaller = nullptr;

    // =========================================================
    // SSH connection widgets
    // =========================================================

    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_patchDirectoryLabel = nullptr;

    QPushButton *m_checkButton = nullptr;

    // =========================================================
    // Patch management widgets
    // =========================================================

    QPushButton *m_refreshButton = nullptr;

    QPushButton *m_installButton = nullptr;
    QPushButton *m_uninstallButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QTableWidget *m_patchTable = nullptr;

    // =========================================================
    // Logs
    // =========================================================

    QPlainTextEdit *m_logEdit = nullptr;

    // =========================================================
    // Patch state
    // =========================================================

    QList<PatchInfo> m_patches;

    bool m_patchBusy = false;
    bool m_connectionBusy = false;
};
