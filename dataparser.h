#ifndef DATAPARSER_H
#define DATAPARSER_H

#include <QByteArray>
#include <vector>

#include "dataframe.h"

class DataParser
{
public:
    DataParser() = default;

    // Add newly received TCP data.
    //
    // Every complete frame parsed from the TCP stream
    // is returned in the vector.
    std::vector<DataFrame> appendData(const QByteArray &data);

private:
    QByteArray buffer;
};

#endif // DATAPARSER_H