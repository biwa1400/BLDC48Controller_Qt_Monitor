
#pragma once

#include <QString>

class QWidget;

class PatchZipImporter
{
public:
    // Import a ZIP archive into the patches directory.
    //
    // The archive must contain exactly one patch package
    // with a manifest.json file.
    //
    // Existing patch directories must not be overwritten.
    //
    // On success:
    //   - The patch is extracted.
    //   - The destination path is returned.
    //
    // On failure:
    //   - An error message is returned.
    //   - No existing patch is modified.

    static bool importZip(
        const QString &zipFilePath,
        const QString &patchesDirectory,
        QString &importedPatchDirectory,
        QString &errorMessage);
};
