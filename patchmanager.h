
#pragma once

#include <QObject>
#include <QString>
#include <QList>

struct PatchInfo
{
    QString id;
    QString name;
    QString version;
    QString description;

    QString directory;
    QString installScript;
    QString uninstallScript;

    bool valid = false;
    QString error;
};

class PatchManager : public QObject
{
    Q_OBJECT

public:
    explicit PatchManager(QObject *parent = nullptr);

    void setPatchDirectory(const QString &directory);
    QString patchDirectory() const;

    QList<PatchInfo> scanPatches() const;
    PatchInfo findPatch(const QString &id) const;

private:
    PatchInfo loadPatch(const QString &directory) const;

    QString m_patchDirectory;
};
