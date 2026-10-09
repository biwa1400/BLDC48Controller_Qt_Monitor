#ifndef DATACACHE_H
#define DATACACHE_H

#include <deque>
#include <cstddef>
#include <chrono>

#include "dataframe.h"

class DataCache
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit DataCache(std::size_t maxFrames = 10000)
        : maxFrames(maxFrames)
    {
    }

    // Keep the most recent five seconds of frames by local arrival time.
    // The legacy constructor argument remains for source compatibility.
    void push(const DataFrame &frame)
    {
        const auto now = Clock::now();
        frames.push_back(frame);
        arrivalTimes.push_back(now);
        const auto cutoff = now - std::chrono::seconds(5);
        while (!arrivalTimes.empty() && arrivalTimes.front() < cutoff) {
            frames.pop_front();
            arrivalTimes.pop_front();
        }
    }

    std::size_t size() const
    {
        return frames.size();
    }

    bool empty() const
    {
        return frames.empty();
    }

    // Index 0 is oldest; size()-1 is newest.
    const DataFrame &at(std::size_t index) const
    {
        return frames.at(index);
    }

    const DataFrame &latest() const
    {
        return frames.back();
    }

    // Monotonic timestamp captured when this frame entered the cache.
    TimePoint timestampAt(std::size_t index) const
    {
        return arrivalTimes.at(index);
    }

    TimePoint latestTimestamp() const
    {
        return arrivalTimes.back();
    }

    void clear()
    {
        frames.clear();
        arrivalTimes.clear();
    }

private:
    std::deque<DataFrame> frames;
    std::deque<TimePoint> arrivalTimes;
    std::size_t maxFrames;
};

#endif // DATACACHE_H
