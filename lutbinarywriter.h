
#ifndef LUTBINARYWRITER_H
#define LUTBINARYWRITER_H

#include "lutcsvparser.h"

#include <QString>
#include <QtGlobal>

class LutBinaryWriter
{
public:
    static constexpr qsizetype BytesPerEntry = 4;
    static constexpr qsizetype BinarySize =
        LutCsvParser::EntryCount * BytesPerEntry;

    struct Result
    {
        bool success = false;
        QString errorMessage;
        QString filePath;
        qsizetype bytesWritten = 0;
    };

    static Result writeFile(
        const QString &filePath,
        const LutCsvParser::LutData &data);
};

#endif // LUTBINARYWRITER_H
