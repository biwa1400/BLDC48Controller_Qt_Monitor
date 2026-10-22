#include "patchzipimporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace {

bool safeFileName(const QString &name)
{
    static const QRegularExpression re(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
    return re.match(name).hasMatch() &&
           name != QStringLiteral(".") &&
           name != QStringLiteral("..");
}

QString base64Utf8(const QString &value)
{
    return QString::fromLatin1(value.toUtf8().toBase64());
}

// PowerShell is used only as a Windows ZIP reader. Every archive entry
// is validated before any file is extracted.
QString extractionScript(const QString &zip, const QString &staging)
{
    static const QString body = QString::fromUtf8(R"PS(
$ErrorActionPreference = 'Stop'
$zipPath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('%1'))
$destination = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('%2'))
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $root = $null
    $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $total = [long]0
    $count = 0
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName
        if ($name.Contains('\') -or $name.StartsWith('/') -or $name.Contains(':')) {
            throw "Unsafe ZIP entry: $name"
        }
        $isDirectory = $name.EndsWith('/')
        $parts = $name.TrimEnd('/').Split('/')
        if ($parts.Count -lt 1 -or $parts.Count -gt 2) {
            throw "Unexpected ZIP structure: $name"
        }
        foreach ($part in $parts) {
            if ($part -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$' -or $part -eq '.' -or $part -eq '..') {
                throw "Unsafe ZIP name: $name"
            }
        }
        if ($null -eq $root) { $root = $parts[0] }
        if ($parts[0] -cne $root) { throw 'ZIP must contain exactly one patch folder.' }
        if ($isDirectory -and $parts.Count -ne 1) { throw 'Nested directories are not supported.' }
        if (-not $isDirectory -and $parts.Count -ne 2) { throw 'Files must be inside the patch folder.' }
        $unixType = ($entry.ExternalAttributes -shr 16) -band 61440
        if ($unixType -eq 40960) { throw "Symbolic links are not allowed: $name" }
        if ($unixType -ne 0 -and $unixType -ne 32768 -and $unixType -ne 16384) {
            throw "Unsupported ZIP entry type: $name"
        }
        if (-not $names.Add($name)) { throw "Duplicate ZIP entry: $name" }
        if (-not $isDirectory) {
            $count++
            $total += $entry.Length
            if ($count -gt 100 -or $total -gt 20971520) {
                throw 'ZIP exceeds the import limits (100 files / 20 MiB).'
            }
        }
    }
    if ($count -eq 0) { throw 'ZIP contains no files.' }
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        $relative = $entry.FullName.Replace('/', [IO.Path]::DirectorySeparatorChar)
        $target = [IO.Path]::Combine($destination, $relative)
        $parent = [IO.Path]::GetDirectoryName($target)
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
    }
}
finally {
    $archive.Dispose()
}
)PS");
    return body.arg(base64Utf8(zip), base64Utf8(staging));
}

bool validScript(const QDir &directory, const QString &script)
{
    if (!safeFileName(script))
        return false;
    const QFileInfo info(directory.filePath(script));
    return info.isFile() && !info.isSymLink();
}

} // namespace

bool PatchZipImporter::importZip(
    const QString &zipFilePath,
    const QString &patchesDirectory,
    QString &importedPatchDirectory,
    QString &errorMessage)
{
    importedPatchDirectory.clear();
    errorMessage.clear();

    const QFileInfo zipInfo(zipFilePath);
    if (!zipInfo.isFile() || zipInfo.isSymLink() ||
        zipInfo.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0) {
        errorMessage = QStringLiteral("Select a regular .zip file.");
        return false;
    }

    if (!QDir().mkpath(patchesDirectory)) {
        errorMessage = QStringLiteral("Cannot create patches directory.");
        return false;
    }

    const QString patchesRoot = QDir(patchesDirectory).absolutePath();
    QTemporaryDir temporary(QDir(patchesRoot).filePath(
        QStringLiteral(".patch-import-XXXXXX")));
    if (!temporary.isValid()) {
        errorMessage = QStringLiteral("Cannot create temporary import directory.");
        return false;
    }

    const QString script = extractionScript(zipInfo.absoluteFilePath(), temporary.path());
    const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()),
                           script.size() * int(sizeof(char16_t)));
    const QString encoded = QString::fromLatin1(utf16.toBase64());

    QProcess process;
    process.start(QStringLiteral("powershell.exe"), {
                                                        QStringLiteral("-NoProfile"),
                                                        QStringLiteral("-NonInteractive"),
                                                        QStringLiteral("-EncodedCommand"), encoded
                                                    });

    if (!process.waitForStarted(10000)) {
        errorMessage = QStringLiteral("Cannot start Windows PowerShell: %1")
        .arg(process.errorString());
        return false;
    }
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(3000);
        errorMessage = QStringLiteral("ZIP extraction timed out or failed.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        errorMessage = QStringLiteral("ZIP extraction rejected: %1")
        .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        return false;
    }

    const QDir stage(temporary.path());
    const QFileInfoList roots = stage.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    if (roots.size() != 1 || !roots.first().isDir() ||
        !safeFileName(roots.first().fileName())) {
        errorMessage = QStringLiteral("ZIP must contain exactly one patch folder.");
        return false;
    }

    const QString folderName = roots.first().fileName();
    const QDir patchDir(roots.first().absoluteFilePath());
    QFile manifest(patchDir.filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("Patch manifest.json is missing.");
        return false;
    }
    QJsonParseError jsonError;
    const QByteArray manifestData = manifest.readAll();
    manifest.close(); // Release the Windows file handle before moving the folder.
    const QJsonDocument doc = QJsonDocument::fromJson(manifestData, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
        errorMessage = QStringLiteral("Invalid manifest.json: %1")
        .arg(jsonError.errorString());
        return false;
    }

    const QJsonObject data = doc.object();
    const QString id = data.value(QStringLiteral("id")).toString();
    const QString install = data.value(QStringLiteral("install")).toString();
    const QString uninstall = data.value(QStringLiteral("uninstall")).toString();
    if (!safeFileName(id) || id != folderName ||
        data.value(QStringLiteral("name")).toString().trimmed().isEmpty() ||
        data.value(QStringLiteral("version")).toString().trimmed().isEmpty() ||
        !validScript(patchDir, install) || !validScript(patchDir, uninstall)) {
        errorMessage = QStringLiteral(
            "Invalid manifest: id must match the folder and install/uninstall scripts must exist.");
        return false;
    }

    const QString destination = QDir(patchesRoot).filePath(folderName);
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) {
        errorMessage = QStringLiteral("Patch already exists; existing patches are never overwritten: %1")
        .arg(destination);
        return false;
    }

    // Both directories are under patchesRoot, so this is a same-volume rename.
    // In particular, manifest.json must be closed before reaching this point.
    if (!QDir(patchesRoot).rename(patchDir.absolutePath(), destination)) {
        errorMessage = QStringLiteral(
                           "Cannot move imported patch into: %1\n"
                           "Check folder permissions, antivirus locks, and whether the target already exists.")
                           .arg(destination);
        return false;
    }

    importedPatchDirectory = destination;
    return true;
}
