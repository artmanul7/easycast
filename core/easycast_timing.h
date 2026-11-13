#pragma once

#include <chrono>
#include <cstdio>
#include <cstdint>

// Each call site owns its accumulator on its own thread; no locks in the hot path.
class EasycastTiming {
public:
    using Clock = std::chrono::steady_clock;
    explicit EasycastTiming(const char *label) : label(label), intervalStart(Clock::now()) {}
    void record(Clock::time_point start) {
        const auto now = Clock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        totalUs += static_cast<uint64_t>(us);
        if (static_cast<uint64_t>(us) > maxUs) maxUs = static_cast<uint64_t>(us);
        ++count;
        const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(now - intervalStart).count();
        if (interval >= 5000) {
            std::fprintf(stderr, "EASYCAST_TIMING kind=%s interval_ms=%lld count=%llu total_us=%llu max_us=%llu\n",
                label, static_cast<long long>(interval), static_cast<unsigned long long>(count),
                static_cast<unsigned long long>(totalUs), static_cast<unsigned long long>(maxUs));
            count = totalUs = maxUs = 0;
            intervalStart = now;
        }
    }
private:
    const char *label;
    Clock::time_point intervalStart;
    uint64_t count = 0, totalUs = 0, maxUs = 0;
};
