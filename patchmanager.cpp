
#include "patchmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

PatchManager::PatchManager(QObject *parent)
    : QObject(parent)
    , m_patchDirectory(
          QDir(QCoreApplication::applicationDirPath())
              .filePath(QStringLiteral("patches")))
{
}

void PatchManager::setPatchDirectory(const QString &directory)
{
    m_patchDirectory = QDir(directory).absolutePath();
}

QString PatchManager::patchDirectory() const
{
    return m_patchDirectory;
}

QList<PatchInfo> PatchManager::scanPatches() const
{
    QList<PatchInfo> patches;

    QDir root(m_patchDirectory);

    if (!root.exists())
        return patches;

    const QFileInfoList directories = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);

    for (const QFileInfo &directory : directories) {
        patches.append(loadPatch(directory.absoluteFilePath()));
    }

    return patches;
}

PatchInfo PatchManager::findPatch(const QString &id) const
{
    const QList<PatchInfo> patches = scanPatches();

    for (const PatchInfo &patch : patches) {
        if (patch.id == id)
            return patch;
    }

    PatchInfo result;
    result.id = id;
    result.error = QStringLiteral("Patch not found.");
    return result;
}

PatchInfo PatchManager::loadPatch(const QString &directory) const
{
    PatchInfo patch;
    patch.directory = QDir(directory).absolutePath();

    const QDir patchDir(patch.directory);
    const QString manifestPath =
        patchDir.filePath(QStringLiteral("manifest.json"));

    QFile manifestFile(manifestPath);

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        patch.error = QStringLiteral(
            "Cannot open manifest.json.");
        return patch;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        manifestFile.readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        patch.error = QStringLiteral("Invalid manifest.json: %1")
        .arg(parseError.errorString());
        return patch;
    }

    const QJsonObject object = document.object();

    patch.id = object.value(
                         QStringLiteral("id")).toString().trimmed();

    patch.name = object.value(
                           QStringLiteral("name")).toString().trimmed();

    patch.version = object.value(
                              QStringLiteral("version")).toString().trimmed();

    patch.description = object.value(
                                  QStringLiteral("description")).toString().trimmed();

    if (patch.id.isEmpty() ||
        patch.name.isEmpty() ||
        patch.version.isEmpty()) {
        patch.error = QStringLiteral(
            "Missing required patch metadata.");
        return patch;
    }

    if (patch.id != patchDir.dirName()) {
        patch.error = QStringLiteral(
            "Patch ID does not match directory name.");
        return patch;
    }

    const QString installName = object.value(
                                          QStringLiteral("install")).toString().trimmed();

    const QString uninstallName = object.value(
                                            QStringLiteral("uninstall")).toString().trimmed();

    if (installName.isEmpty() || uninstallName.isEmpty()) {
        patch.error = QStringLiteral(
            "Missing install or uninstall script.");
        return patch;
    }

    const auto resolveScript =
        [&patchDir](const QString &name) -> QString {
        if (name == QStringLiteral(".") ||
            name == QStringLiteral("..") ||
            name.contains(QLatin1Char('/')) ||
            name.contains(QLatin1Char('\\')) ||
            name.contains(QLatin1Char(':'))) {
            return {};
        }

        const QFileInfo fileInfo(patchDir.filePath(name));

        if (!fileInfo.isFile() || fileInfo.isSymLink())
            return {};

        return fileInfo.absoluteFilePath();
    };

    patch.installScript = resolveScript(installName);
    patch.uninstallScript = resolveScript(uninstallName);

    if (patch.installScript.isEmpty()) {
        patch.error = QStringLiteral(
            "Install script is missing or invalid.");
        return patch;
    }

    if (patch.uninstallScript.isEmpty()) {
        patch.error = QStringLiteral(
            "Uninstall script is missing or invalid.");
        return patch;
    }

    patch.valid = true;
    return patch;
}
