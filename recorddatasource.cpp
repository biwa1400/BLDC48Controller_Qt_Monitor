#include "recorddatasource.h"

#include <QByteArray>

#include <algorithm>
#include <limits>


namespace
{
constexpr qint64 SEARCH_BYTES = 1024 * 1024;

bool looksLikeFrame(
    const QByteArray &data,
    qint64 offset)
{
    if (offset < 0 ||
        offset + RecordDataSource::FRAME_SIZE_BYTES > data.size())
    {
        return false;
    }

    const char *p =
        data.constData() + offset;

    return
        p[0] == 'b' &&
        p[1] == 'g' &&
        p[RecordDataSource::FRAME_SIZE_BYTES - 2] == 'e' &&
        p[RecordDataSource::FRAME_SIZE_BYTES - 1] == 'd';
}
}


RecordDataSource::~RecordDataSource()
{
    close();
}


bool RecordDataSource::open(
    const QString &fileName)
{
    close();

    m_fileName = fileName;
    m_file.setFileName(fileName);

    if (!m_file.open(QIODevice::ReadOnly))
    {
        m_lastError =
            QString(
                "Could not open Record file: %1")
                .arg(m_file.errorString());

        return false;
    }

    m_fileSize =
        m_file.size();

    if (m_fileSize < FRAME_SIZE_BYTES)
    {
        m_lastError =
            "Record file is smaller than one complete frame.";

        close();
        return false;
    }

    if (!locateFirstFrame())
    {
        close();
        return false;
    }

    const qint64 availableBytes =
        m_fileSize -
        m_firstFrameOffset;

    m_totalFrames =
        availableBytes /
        FRAME_SIZE_BYTES;

    if (m_totalFrames <= 0)
    {
        m_lastError =
            "Record file contains no complete frames.";

        close();
        return false;
    }

    // Mapping is an optimization, not a requirement for opening.
    // If mapping fails, normal QFile reads still work.
    mapFrameRegion();

    m_lastError.clear();
    return true;
}


void RecordDataSource::close()
{
    if (m_mappedData != nullptr)
    {
        m_file.unmap(
            m_mappedData);

        m_mappedData = nullptr;
    }

    m_mappedSize = 0;

    if (m_file.isOpen())
    {
        m_file.close();
    }

    m_fileName.clear();
    m_fileSize = 0;
    m_firstFrameOffset = 0;
    m_totalFrames = 0;
}


bool RecordDataSource::isOpen() const
{
    return m_file.isOpen();
}


QString RecordDataSource::fileName() const
{
    return m_fileName;
}


qint64 RecordDataSource::fileSize() const
{
    return m_fileSize;
}


qint64 RecordDataSource::totalFrames() const
{
    return m_totalFrames;
}


qint64 RecordDataSource::firstFrameOffset() const
{
    return m_firstFrameOffset;
}


QString RecordDataSource::lastError() const
{
    return m_lastError;
}


bool RecordDataSource::isMemoryMapped() const
{
    return m_mappedData != nullptr;
}


bool RecordDataSource::locateFirstFrame()
{
    if (!m_file.seek(0))
    {
        m_lastError =
            "Could not seek to the beginning of the Record file.";

        return false;
    }

    const qint64 bytesToRead =
        std::min<qint64>(
            m_fileSize,
            SEARCH_BYTES);

    const QByteArray probe =
        m_file.read(bytesToRead);

    if (probe.size() < FRAME_SIZE_BYTES)
    {
        m_lastError =
            "Could not read enough data to locate a Record frame.";

        return false;
    }

    const qint64 lastCandidate =
        static_cast<qint64>(probe.size()) -
        FRAME_SIZE_BYTES;

    for (qint64 offset = 0;
         offset <= lastCandidate;
         ++offset)
    {
        if (!looksLikeFrame(
                probe,
                offset))
        {
            continue;
        }

        // If another complete frame is available in the probe,
        // verify the expected fixed 60-byte spacing as well.
        const qint64 nextOffset =
            offset +
            FRAME_SIZE_BYTES;

        if (nextOffset + FRAME_SIZE_BYTES <= probe.size())
        {
            if (!looksLikeFrame(
                    probe,
                    nextOffset))
            {
                continue;
            }
        }

        m_firstFrameOffset =
            offset;

        return true;
    }

    m_lastError =
        "Could not locate a valid 60-byte Record frame in the first 1 MB.";

    return false;
}


bool RecordDataSource::mapFrameRegion()
{
    if (!m_file.isOpen() ||
        m_totalFrames <= 0)
    {
        return false;
    }

    // Map from file offset 0. This avoids platform-specific
    // alignment restrictions on non-zero mapping offsets.
    // m_firstFrameOffset is applied when accessing frame data.
    m_mappedSize =
        m_fileSize;

    m_mappedData =
        m_file.map(
            0,
            m_mappedSize);

    if (m_mappedData == nullptr)
    {
        m_mappedSize = 0;
        return false;
    }

    return true;
}


QByteArray RecordDataSource::frameBytes(
    qint64 firstFrame,
    qint64 frameCount)
{
    if (!m_file.isOpen() ||
        firstFrame < 0 ||
        frameCount <= 0 ||
        firstFrame >= m_totalFrames)
    {
        return {};
    }

    frameCount =
        std::min<qint64>(
            frameCount,
            m_totalFrames - firstFrame);

    const qint64 byteOffset =
        firstFrame *
        FRAME_SIZE_BYTES;

    const qint64 byteCount =
        frameCount *
        FRAME_SIZE_BYTES;

    if (m_mappedData != nullptr)
    {
        return QByteArray(
            reinterpret_cast<const char *>(
                m_mappedData +
                m_firstFrameOffset +
                byteOffset),
            static_cast<qsizetype>(
                byteCount));
    }

    if (!m_file.seek(
            m_firstFrameOffset +
            byteOffset))
    {
        m_lastError =
            "Could not seek to the requested Record frame.";

        return {};
    }

    QByteArray bytes =
        m_file.read(byteCount);

    if (bytes.size() != byteCount)
    {
        m_lastError =
            QString(
                "Short Record read: expected %1 bytes, got %2 bytes.")
                .arg(byteCount)
                .arg(bytes.size());

        return {};
    }

    return bytes;
}


std::vector<DataFrame> RecordDataSource::readFrames(
    qint64 firstFrame,
    qint64 frameCount)
{
    std::vector<DataFrame> result;

    if (!m_file.isOpen())
    {
        m_lastError =
            "No Record file is open.";

        return result;
    }

    if (firstFrame < 0 ||
        frameCount <= 0 ||
        firstFrame >= m_totalFrames)
    {
        m_lastError =
            "Invalid Record frame range.";

        return result;
    }

    if (frameCount > MAX_DIRECT_FRAMES)
    {
        m_lastError =
            QString(
                "Direct Record read is limited to %1 frames.")
                .arg(MAX_DIRECT_FRAMES);

        return result;
    }

    frameCount =
        std::min<qint64>(
            frameCount,
            m_totalFrames - firstFrame);

    const QByteArray bytes =
        frameBytes(
            firstFrame,
            frameCount);

    if (bytes.isEmpty())
    {
        if (m_lastError.isEmpty())
        {
            m_lastError =
                "Record read returned no data.";
        }

        return result;
    }

    DataParser parser;

    result =
        parser.appendData(bytes);

    if (result.empty())
    {
        m_lastError =
            "Record bytes were read, but DataParser returned no frames.";

        return result;
    }

    m_lastError.clear();
    return result;
}


std::vector<RecordDataSource::SampledFrame>
RecordDataSource::readFramesStrided(
    qint64 firstFrame,
    qint64 lastFrame,
    qint64 stride,
    bool includeLastFrame)
{
    std::vector<SampledFrame> result;

    if (!m_file.isOpen())
    {
        m_lastError =
            "No Record file is open.";

        return result;
    }

    if (stride <= 0 ||
        firstFrame < 0 ||
        lastFrame < firstFrame ||
        firstFrame >= m_totalFrames)
    {
        m_lastError =
            "Invalid strided Record frame range.";

        return result;
    }

    lastFrame =
        std::min<qint64>(
            lastFrame,
            m_totalFrames - 1);

    const qint64 regularCount =
        ((lastFrame - firstFrame) /
         stride) + 1;

    const qint64 lastRegularFrame =
        firstFrame +
        (regularCount - 1) *
            stride;

    const bool appendExactLast =
        includeLastFrame &&
        lastRegularFrame != lastFrame;

    const qint64 outputCount =
        regularCount +
        (appendExactLast ? 1 : 0);

    if (outputCount <= 0)
    {
        return result;
    }

    if (outputCount >
        static_cast<qint64>(
            std::numeric_limits<int>::max()))
    {
        m_lastError =
            "Requested strided Record view is too large.";

        return result;
    }

    QByteArray packedFrames;

    packedFrames.reserve(
        static_cast<qsizetype>(
            outputCount *
            FRAME_SIZE_BYTES));

    std::vector<qint64> frameIndices;

    frameIndices.reserve(
        static_cast<std::size_t>(
            outputCount));

    auto appendFrameBytes =
        [this,
         &packedFrames,
         &frameIndices](qint64 frameIndex)
    {
        const qint64 relativeByteOffset =
            frameIndex *
            FRAME_SIZE_BYTES;

        if (m_mappedData != nullptr)
        {
            packedFrames.append(
                reinterpret_cast<const char *>(
                    m_mappedData +
                    m_firstFrameOffset +
                    relativeByteOffset),
                static_cast<qsizetype>(
                    FRAME_SIZE_BYTES));

            frameIndices.push_back(
                frameIndex);

            return true;
        }

        if (!m_file.seek(
                m_firstFrameOffset +
                relativeByteOffset))
        {
            return false;
        }

        const QByteArray oneFrame =
            m_file.read(
                FRAME_SIZE_BYTES);

        if (oneFrame.size() !=
            FRAME_SIZE_BYTES)
        {
            return false;
        }

        packedFrames.append(
            oneFrame);

        frameIndices.push_back(
            frameIndex);

        return true;
    };


    for (qint64 frameIndex = firstFrame;
         frameIndex <= lastRegularFrame;
         frameIndex += stride)
    {
        if (!appendFrameBytes(
                frameIndex))
        {
            m_lastError =
                "Could not read a sampled Record frame.";

            return {};
        }
    }

    if (appendExactLast)
    {
        if (!appendFrameBytes(
                lastFrame))
        {
            m_lastError =
                "Could not read the final sampled Record frame.";

            return {};
        }
    }

    DataParser parser;

    const std::vector<DataFrame> parsedFrames =
        parser.appendData(
            packedFrames);

    if (parsedFrames.empty())
    {
        m_lastError =
            "Sampled Record bytes were read, but DataParser returned no frames.";

        return result;
    }

    const std::size_t count =
        std::min(
            parsedFrames.size(),
            frameIndices.size());

    result.reserve(count);

    for (std::size_t i = 0;
         i < count;
         ++i)
    {
        SampledFrame sampled;

        sampled.frameIndex =
            frameIndices[i];

        sampled.frame =
            parsedFrames[i];

        result.push_back(
            sampled);
    }

    m_lastError.clear();
    return result;
}