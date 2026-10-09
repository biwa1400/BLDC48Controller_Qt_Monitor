#include "dataparser.h"

#include <QtEndian>
#include <QDebug>
#include <vector>

std::vector<DataFrame> DataParser::appendData(const QByteArray &data)
{
    buffer.append(data);

    constexpr int PAYLOAD_SIZE = 108;
    constexpr int FRAME_SIZE = PAYLOAD_SIZE + 4;
    const QByteArray frameStart("bg", 2);
    const QByteArray frameEnd("ed", 2);

    std::vector<DataFrame> parsedFrames;
    static quint64 validFrames = 0;
    static quint64 badEnds = 0;

    while (true) {
        const int startIndex = buffer.indexOf(frameStart);
        if (startIndex < 0) {
            if (!buffer.isEmpty() && buffer.endsWith('b')) {
                buffer = QByteArray(1, 'b');
            } else {
                buffer.clear();
            }
            break;
        }
        if (startIndex > 0)
            buffer.remove(0, startIndex);

        if (buffer.size() < FRAME_SIZE)
            break;

        if (buffer.mid(FRAME_SIZE - 2, 2) != frameEnd) {
            ++badEnds;
            if (badEnds <= 5 || badEnds % 1000 == 0) {
                qWarning().noquote() << "[DMA RX] BAD END, count=" << badEnds
                                     << "tail=" << buffer.mid(110, 2).toHex(' ');
            }
            buffer.remove(0, 1);
            continue;
        }

        const QByteArray payload = buffer.mid(2, PAYLOAD_SIZE);
        const uchar *p = reinterpret_cast<const uchar *>(payload.constData());
        DataFrame frame;

        // FPGA concatenation: 44 bytes Filter, 44 bytes Peak,
        // 4 bytes sign-extended RPM, 4 bytes LM10 Encoder,
        // 2 bytes VS Rho, 2 bytes Phase Advance, 2 bytes DTC, 6 bytes padding.
        // DMA transfers little-endian 32-bit words, lowest bytes first.
        for (int i = 0; i < 22; ++i)
            frame.filteredCurrents[i] = qFromLittleEndian<qint16>(p + i * 2);

        for (int i = 0; i < 22; ++i)
            frame.currents[i] = qFromLittleEndian<qint16>(p + 44 + i * 2);

        frame.rpm = qFromLittleEndian<qint16>(p + 88);
        frame.vsRho = qFromLittleEndian<quint16>(p + 96);
        frame.phaseAdv = qFromLittleEndian<quint16>(p + 98);
        frame.dtc = qFromLittleEndian<quint16>(p + 100);
        frame.degree = qFromLittleEndian<quint32>(p + 92);

        ++validFrames;
        if (validFrames <= 5 || validFrames % 1000 == 0) {
            qDebug().noquote()
            << "[DMA RX] frame=" << validFrames
            << "rpm=" << frame.rpm
            << "rpmRaw=" << payload.mid(88, 4).toHex(' ')
            << "encoder=" << frame.degree
            << "encoderRaw=" << payload.mid(92, 4).toHex(' ')
            << "vsRho=" << frame.vsRho
            << "phaseAdv=" << frame.phaseAdv
            << "dtc=" << frame.dtc
            << "padding=" << payload.mid(102, 6).toHex(' ');
        }

        parsedFrames.push_back(frame);
        buffer.remove(0, FRAME_SIZE);
    }

    return parsedFrames;
}
