#ifndef RECORDDATASOURCE_H
#define RECORDDATASOURCE_H

#include <QFile>
#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <vector>

#include "dataparser.h"


class RecordDataSource
{
public:
    static constexpr qint64 FRAME_SIZE_BYTES = 60;
    static constexpr qint64 MAX_DIRECT_FRAMES = 20000;

    struct SampledFrame
    {
        qint64 frameIndex = 0;
        DataFrame frame;
    };

    RecordDataSource() = default;
    ~RecordDataSource();

    bool open(const QString &fileName);
    void close();

    bool isOpen() const;

    QString fileName() const;
    qint64 fileSize() const;
    qint64 totalFrames() const;
    qint64 firstFrameOffset() const;
    QString lastError() const;

    // Read one contiguous range. Intended for zoomed-in views.
    std::vector<DataFrame> readFrames(
        qint64 firstFrame,
        qint64 frameCount);

    // Read only every Nth frame from a potentially very large range.
    // The file is memory mapped, so this does not issue thousands of
    // QFile seek/read calls and does not load the whole file into RAM.
    std::vector<SampledFrame> readFramesStrided(
        qint64 firstFrame,
        qint64 lastFrame,
        qint64 stride,
        bool includeLastFrame = true);

    bool isMemoryMapped() const;

private:
    bool locateFirstFrame();
    bool mapFrameRegion();

    QByteArray frameBytes(
        qint64 firstFrame,
        qint64 frameCount);

    QFile m_file;

    QString m_fileName;
    QString m_lastError;

    qint64 m_fileSize = 0;
    qint64 m_firstFrameOffset = 0;
    qint64 m_totalFrames = 0;

    uchar *m_mappedData = nullptr;
    qint64 m_mappedSize = 0;
};


#endif // RECORDDATASOURCE_H