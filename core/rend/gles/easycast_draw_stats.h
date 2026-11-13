#pragma once
#include "easycast_timing.h"

// Counters are per thread: texture lookups and drawing are done by the render thread.
struct EasycastDrawStats {
    EasycastTiming::Clock::time_point start = EasycastTiming::Clock::now();
    uint64_t frames = 0, draws = 0, sampled = 0, stateUs = 0, drawUs = 0;
    uint64_t textures = 0, textureUpdates = 0, textureUpdateUs = 0;
    uint64_t sequence = 0;
    void report() {
        const auto now = EasycastTiming::Clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (ms < 5000) return;
        std::fprintf(stderr, "EASYCAST_DRAW interval_ms=%lld frames=%llu draws=%llu sampled=%llu state_us=%llu draw_us=%llu texture_lookups=%llu texture_updates=%llu texture_update_us=%llu\n",
            static_cast<long long>(ms), (unsigned long long)frames, (unsigned long long)draws,
            (unsigned long long)sampled, (unsigned long long)stateUs, (unsigned long long)drawUs,
            (unsigned long long)textures, (unsigned long long)textureUpdates, (unsigned long long)textureUpdateUs);
        frames = draws = sampled = stateUs = drawUs = textures = textureUpdates = textureUpdateUs = 0;
        start = now;
    }
};
extern thread_local EasycastDrawStats easycastDrawStats;
