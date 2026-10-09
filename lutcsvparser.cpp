
#include "lutcsvparser.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>

#include <cstddef>

LutCsvParser::Result LutCsvParser::parseFile(const QString &filePath)
{
    Result result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage =
            QString("Cannot open CSV file: %1").arg(file.errorString());
        return result;
    }

    QTextStream stream(&file);
    std::size_t count = 0;
    qsizetype lineNumber = 0;
    int columnCount = 0;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        ++lineNumber;

        if (lineNumber == 1 && line.startsWith(QChar(0xFEFF)))
            line.remove(0, 1);

        if (line.isEmpty())
            continue;

        const QStringList fields = line.split(',', Qt::KeepEmptyParts);

        if (columnCount == 0) {
            bool isHeader = false;

            if (fields.size() == 1) {
                isHeader = fields[0].trimmed().compare(
                               "value", Qt::CaseInsensitive) == 0;
            } else if (fields.size() == 2) {
                isHeader =
                    fields[0].trimmed().compare(
                        "index", Qt::CaseInsensitive) == 0 &&
                    fields[1].trimmed().compare(
                        "value", Qt::CaseInsensitive) == 0;
            }

            if (isHeader) {
                columnCount = fields.size();
                continue;
            }

            if (fields.size() != 1 && fields.size() != 2) {
                result.errorMessage =
                    QString("Line %1: Expected one or two columns.")
                        .arg(lineNumber);
                return result;
            }

            columnCount = fields.size();
        }

        if (fields.size() != columnCount) {
            result.errorMessage =
                QString("Line %1: Inconsistent column count.")
                    .arg(lineNumber);
            return result;
        }

        if (count >= EntryCount) {
            result.errorMessage =
                QString("Line %1: More than %2 LUT entries.")
                    .arg(lineNumber)
                    .arg(EntryCount);
            return result;
        }

        int valueColumn = 0;

        if (columnCount == 2) {
            bool indexOk = false;
            const uint index =
                fields[0].trimmed().toUInt(&indexOk, 10);

            if (!indexOk || index != count) {
                result.errorMessage =
                    QString("Line %1: Expected index %2.")
                        .arg(lineNumber)
                        .arg(count);
                return result;
            }

            valueColumn = 1;
        }

        bool valueOk = false;
        const uint value =
            fields[valueColumn].trimmed().toUInt(&valueOk, 10);

        if (!valueOk || value > MaxValue) {
            result.errorMessage =
                QString("Line %1: Value must be an integer from 0 to %2.")
                    .arg(lineNumber)
                    .arg(MaxValue);
            return result;
        }

        result.data[count] = static_cast<std::uint16_t>(value);
        ++count;
    }

    if (stream.status() != QTextStream::Ok) {
        result.errorMessage = "Error while reading CSV file.";
        return result;
    }

    if (count != EntryCount) {
        result.errorMessage =
            QString("Expected %1 LUT entries, but found %2.")
                .arg(EntryCount)
                .arg(count);
        return result;
    }

    result.success = true;
    return result;
}
