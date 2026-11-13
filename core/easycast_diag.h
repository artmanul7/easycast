// EasyCast 0.2.2 (Claude): what a stalled frame was waiting for. Cheap counters (atomics, steady clock):
//  - Vulkan pipeline creation (first use of a shader/state combination; per-pixel has many)  -> render thread
//  - texture uploads to the GPU                                                               -> render thread
//  - GD-ROM sector reads from the disc image on the SD card                                   -> emulation thread
// The GPU timer logs "EASYCAST_STALL t_ms= gap_ms= pipes= pipe_ms= tex= tex_ms= disc_ms= ..." for a screen frame
// gap > 150 ms; the dynarec time-slice hook logs "EASYCAST_EMUSTALL" when the emulation thread itself stops > 100 ms.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace easycast_diag {

inline uint64_t nowNs()
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline uint64_t startNs() { static const uint64_t s = nowNs(); return s; }
inline long long tMs() { const uint64_t s = startNs(); return (long long)((nowNs() - s) / 1000000); }

struct Counters {
	std::atomic<uint64_t> pipeNs { 0 }, pipes { 0 }, texNs { 0 }, texs { 0 }, discNs { 0 }, discReads { 0 },
			discMaxNs { 0 };
	// 0.2.4 hitch catcher (easycast_hitch.h): reset by the render thread at every present
	std::atomic<uint64_t> sliceMaxNs { 0 }, audMaxNs { 0 }, presentDrops { 0 };
};

inline void maxTo(std::atomic<uint64_t> &m, uint64_t v)
{
	if (v > m.load(std::memory_order_relaxed))
		m.store(v, std::memory_order_relaxed);		// one writer per counter is enough for a diagnostic maximum
}
inline Counters &c() { static Counters k; return k; }

// drop-in timing of AudioBackend::push() (the audio output may block the thread that feeds it)
struct AudioScope {
	uint64_t t0 = nowNs();
	~AudioScope() { maxTo(c().audMaxNs, nowNs() - t0); }
};

struct Scope {
	std::atomic<uint64_t> &ns, &count;
	uint64_t t0 = nowNs();
	Scope(std::atomic<uint64_t> &n, std::atomic<uint64_t> &k) : ns(n), count(k) {}
	~Scope() { ns.fetch_add(nowNs() - t0, std::memory_order_relaxed); count.fetch_add(1, std::memory_order_relaxed); }
};

// drop-in for device.createGraphicsPipelineUnique(cache, info)
template<typename Device, typename... Args>
inline auto timedPipeline(Device &&device, Args &&...args)
{
	Scope s(c().pipeNs, c().pipes);
	return device.createGraphicsPipelineUnique(std::forward<Args>(args)...);
}

struct DiscScope {
	uint64_t t0 = nowNs();
	~DiscScope()
	{
		uint64_t d = nowNs() - t0;
		c().discNs.fetch_add(d, std::memory_order_relaxed);
		c().discReads.fetch_add(1, std::memory_order_relaxed);
		uint64_t m = c().discMaxNs.load(std::memory_order_relaxed);
		while (d > m && !c().discMaxNs.compare_exchange_weak(m, d)) {}
	}
};

// snapshot of the counters since the previous call from the same caller
struct Delta {
	uint64_t pipeNs = 0, pipes = 0, texNs = 0, texs = 0, discNs = 0, discReads = 0;
	void take(Delta &out)
	{
		Counters &k = c();
		uint64_t v[6] = { k.pipeNs.load(), k.pipes.load(), k.texNs.load(), k.texs.load(), k.discNs.load(), k.discReads.load() };
		out.pipeNs = v[0] - pipeNs; out.pipes = v[1] - pipes; out.texNs = v[2] - texNs; out.texs = v[3] - texs;
		out.discNs = v[4] - discNs; out.discReads = v[5] - discReads;
		pipeNs = v[0]; pipes = v[1]; texNs = v[2]; texs = v[3]; discNs = v[4]; discReads = v[5];
	}
};

// emulation thread: called every 448-cycle time slice
inline void emuSlice()
{
	// 14.11: read the clock every 16th slice only (~36 us of guest time): it cost ~1-2% of the emulation thread.
	// Stalls of 100 ms and the hitch catcher's longest-slice value keep working at that granularity.
	static uint32_t tick = 0;
	if ((++tick & 15) != 0)
		return;
	static uint64_t last = 0;
	static Delta d;
	uint64_t t = nowNs();
	if (last != 0)
		maxTo(c().sliceMaxNs, t - last);
	if (last != 0 && t - last > 100000000ull) {
		Delta x;
		d.take(x);
		std::fprintf(stderr, "EASYCAST_EMUSTALL t_ms=%lld gap_ms=%llu disc_reads=%llu disc_ms=%llu\n", tMs(),
				(unsigned long long)((t - last) / 1000000), (unsigned long long)x.discReads,
				(unsigned long long)(x.discNs / 1000000));
	} else if (last == 0 || (t - last) > 0) {
		static uint64_t lastTake = 0;
		if (t - lastTake > 1000000000ull) { Delta x; d.take(x); lastTake = t; }	// keep the delta short
	}
	last = t;
}

} // namespace easycast_diag
