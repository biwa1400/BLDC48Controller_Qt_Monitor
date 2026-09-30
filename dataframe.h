#ifndef DATAFRAME_H
#define DATAFRAME_H

#include <array>
#include <cstdint>

struct DataFrame
{
    // I1 ~ I22
    std::array<int16_t, 22> currents{};

    int16_t rpm = 0;

    uint16_t vsRho = 0;
    uint16_t phaseAdv = 0;
    uint16_t dtc = 0;

    uint32_t degree = 0;
};

#endif // DATAFRAME_H