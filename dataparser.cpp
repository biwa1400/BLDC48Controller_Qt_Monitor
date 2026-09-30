#include "dataparser.h"

#include <QtEndian>

std::vector<DataFrame> DataParser::appendData(const QByteArray &data)
{
    buffer.append(data);

    constexpr int FRAME_SIZE = 60;
    constexpr int PAYLOAD_SIZE = 56;

    const QByteArray frameStart("bg", 2);
    const QByteArray frameEnd("ed", 2);

    std::vector<DataFrame> parsedFrames;

    while (true) {

        // Find frame header "bg".
        int startIndex = buffer.indexOf(frameStart);

        if (startIndex < 0) {

            // Keep the final 'b' in case "bg" is split
            // between two TCP packets.
            if (!buffer.isEmpty() && buffer.endsWith('b')) {
                buffer = QByteArray(1, 'b');
            } else {
                buffer.clear();
            }

            break;
        }

        // Remove garbage before frame header.
        if (startIndex > 0) {
            buffer.remove(0, startIndex);
        }

        // Wait for more TCP data if the frame is incomplete.
        if (buffer.size() < FRAME_SIZE) {
            break;
        }

        // Check frame footer "ed".
        if (buffer.mid(FRAME_SIZE - 2, 2) != frameEnd) {
            buffer.remove(0, 1);
            continue;
        }

        // Extract the 56-byte payload:
        //
        // [0..1]     "bg"
        // [2..57]    payload
        // [58..59]   "ed"
        //
        const QByteArray payload = buffer.mid(2, PAYLOAD_SIZE);

        const uchar *p =
            reinterpret_cast<const uchar *>(payload.constData());

        DataFrame frame;

        int offset = 0;

        // I1 ~ I22
        for (int i = 0; i < 22; ++i) {

            frame.currents[i] =
                qFromLittleEndian<qint16>(p + offset);

            offset += 2;
        }

        // RPM
        frame.rpm =
            qFromLittleEndian<qint16>(p + offset);
        offset += 2;

        // VS_RHO
        frame.vsRho =
            qFromLittleEndian<quint16>(p + offset);
        offset += 2;

        // PHASE_ADV
        frame.phaseAdv =
            qFromLittleEndian<quint16>(p + offset);
        offset += 2;

        // DTC
        frame.dtc =
            qFromLittleEndian<quint16>(p + offset);
        offset += 2;

        // DEGREE
        frame.degree =
            qFromLittleEndian<quint32>(p + offset);

        // Store this complete frame.
        parsedFrames.push_back(frame);

        // Remove the processed 60-byte frame.
        buffer.remove(0, FRAME_SIZE);
    }

    return parsedFrames;
}