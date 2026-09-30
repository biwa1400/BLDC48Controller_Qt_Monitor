#ifndef DATACACHE_H
#define DATACACHE_H

#include <deque>
#include <cstddef>

#include "dataframe.h"

class DataCache
{
public:
    explicit DataCache(std::size_t maxFrames = 10000)
        : maxFrames(maxFrames)
    {
    }

    // Add one parsed frame to the cache.
    void push(const DataFrame &frame)
    {
        frames.push_back(frame);

        // Keep only the newest maxFrames frames.
        while (frames.size() > maxFrames) {
            frames.pop_front();
        }
    }

    // Number of frames currently stored.
    std::size_t size() const
    {
        return frames.size();
    }

    // Whether the cache is empty.
    bool empty() const
    {
        return frames.empty();
    }

    // Access a frame by index.
    //
    // index 0 = oldest frame
    // index size()-1 = newest frame
    const DataFrame &at(std::size_t index) const
    {
        return frames.at(index);
    }

    // Most recent frame.
    const DataFrame &latest() const
    {
        return frames.back();
    }

    // Remove all cached frames.
    void clear()
    {
        frames.clear();
    }

private:
    std::deque<DataFrame> frames;

    std::size_t maxFrames;
};

#endif // DATACACHE_H