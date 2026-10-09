
#include "lutbinarywriter.h"

#include <QFile>
#include <QSaveFile>
#include <QByteArray>

#include <cstddef>
#include <cstdint>

LutBinaryWriter::Result LutBinaryWriter::writeFile(
    const QString &filePath,
    const LutCsvParser::LutData &data)
{
    Result result;

    if (filePath.trimmed().isEmpty()) {
        result.errorMessage = "Output BIN file path is empty.";
        return result;
    }

    QByteArray binary;
    binary.reserve(static_cast<qsizetype>(BinarySize));

    for (std::uint16_t value : data) {
        if (value > LutCsvParser::MaxValue) {
            result.errorMessage =
                QString("LUT value %1 exceeds the allowed maximum %2.")
                    .arg(value)
                    .arg(LutCsvParser::MaxValue);
            return result;
        }

        const std::uint32_t word = value;

        binary.append(static_cast<char>(word & 0xFFu));
        binary.append(static_cast<char>((word >> 8) & 0xFFu));
        binary.append(static_cast<char>((word >> 16) & 0xFFu));
        binary.append(static_cast<char>((word >> 24) & 0xFFu));
    }

    if (binary.size() != BinarySize) {
        result.errorMessage = "Unexpected LUT binary size.";
        return result;
    }

    QSaveFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        result.errorMessage =
            QString("Cannot create BIN file: %1")
                .arg(file.errorString());
        return result;
    }

    const qint64 written = file.write(binary);

    if (written != binary.size()) {
        result.errorMessage =
            QString("Failed to write BIN file: %1")
                .arg(file.errorString());
        file.cancelWriting();
        return result;
    }

    if (!file.commit()) {
        result.errorMessage =
            QString("Failed to save BIN file: %1")
                .arg(file.errorString());
        return result;
    }

    result.success = true;
    result.filePath = filePath;
    result.bytesWritten = static_cast<qsizetype>(written);

    return result;
}
