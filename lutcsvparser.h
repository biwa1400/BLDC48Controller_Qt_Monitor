
#ifndef LUTCSVPARSER_H
#define LUTCSVPARSER_H

#include <QString>
#include <array>
#include <cstdint>

class LutCsvParser
{
public:
    static constexpr std::size_t EntryCount = 4096;
    static constexpr std::uint16_t MaxValue = 8191;

    using LutData = std::array<std::uint16_t, EntryCount>;

    struct Result
    {
        bool success = false;
        QString errorMessage;
        LutData data{};
    };

    static Result parseFile(const QString &filePath);
};

#endif // LUTCSVPARSER_H
