// EasyCast (Claude): GPU time of the main screen pass via Vulkan timestamps.
// Writes one line every ~5 s:  EASYCAST_GPU interval_ms frames avg_us max_us busy_pct
// Only the screen pass command buffer is measured (ScreenDrawer per-triangle, OITScreenDrawer per-pixel;
// not render-to-texture passes or the final blit). Always on (as in 0.1.6).
#pragma once
#include "vulkan.h"
#include "easycast_gpufreq.h"
#include "easycast_diag.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace easycast_gpu {

constexpr uint32_t SLOTS = 8;  // frames in flight we can keep results for

struct State {
	vk::Device device;
	vk::QueryPool pool;
	double periodNs = 0;
	uint64_t mask = ~0ull;
	bool disabled = false;
	bool written[SLOTS] {};
	uint32_t frame = 0;
	uint64_t sumNs = 0, maxNs = 0;
	uint32_t count = 0;
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
};

inline State &st() { static State s; return s; }

inline bool ensure(vk::Device device, vk::PhysicalDevice phys, uint32_t queueFamily)
{
	State &s = st();
	if (s.disabled)
		return false;
	if (s.pool && s.device == device)
		return true;
	vk::PhysicalDeviceProperties props = phys.getProperties();
	auto families = phys.getQueueFamilyProperties();
	uint32_t bits = queueFamily < families.size() ? families[queueFamily].timestampValidBits : 0;
	if (props.limits.timestampPeriod <= 0 || bits == 0) {
		std::fprintf(stderr, "EASYCAST_GPU unsupported period=%f bits=%u\n", props.limits.timestampPeriod, bits);
		s.disabled = true;
		return false;
	}
	s.periodNs = props.limits.timestampPeriod;
	s.mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
	s.device = device;
	s.pool = device.createQueryPool(vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, SLOTS * 2));
	for (auto &w : s.written) w = false;
	std::fprintf(stderr, "EASYCAST_GPU enabled period_ns=%.3f bits=%u\n", s.periodNs, bits);
	return true;
}

// at the start of the frame's command buffer (outside a render pass)
inline void begin(vk::CommandBuffer cmd, vk::Device device, vk::PhysicalDevice phys, uint32_t queueFamily)
{
	if (!ensure(device, phys, queueFamily))
		return;
	State &s = st();
	{	// 0.2.0: any gap > 150 ms between screen frames is logged with the last GPU clock change (stall hunting)
		static std::chrono::steady_clock::time_point last {};
		auto t = std::chrono::steady_clock::now();
		if (last.time_since_epoch().count() != 0) {
			long long gap = std::chrono::duration_cast<std::chrono::milliseconds>(t - last).count();
			static easycast_diag::Delta dd;
			easycast_diag::Delta x;
			dd.take(x);		// what happened since the previous screen frame
			if (gap > 150) {
				auto &d = easycast_gpufreq::dev();
				long long ago = d.lastChange.time_since_epoch().count() == 0 ? -1 :
						(long long)std::chrono::duration_cast<std::chrono::milliseconds>(t - d.lastChange).count();
				std::fprintf(stderr, "EASYCAST_STALL t_ms=%lld gap_ms=%lld pipes=%llu pipe_ms=%llu tex=%llu tex_ms=%llu "
						"disc_reads=%llu disc_ms=%llu gpufreq_change_ago_ms=%lld to_mhz=%llu\n", easycast_diag::tMs(), gap,
						(unsigned long long)x.pipes, (unsigned long long)(x.pipeNs / 1000000), (unsigned long long)x.texs,
						(unsigned long long)(x.texNs / 1000000), (unsigned long long)x.discReads,
						(unsigned long long)(x.discNs / 1000000), ago, (unsigned long long)d.lastChangeMhz);
			}
		}
		last = t;
	}
	uint32_t slot = s.frame % SLOTS;
	// collect the result this slot held SLOTS frames ago (long done by now; skip if not ready)
	if (s.written[slot]) {
		uint64_t ts[2];
		vk::Result r = device.getQueryPoolResults(s.pool, slot * 2, 2, sizeof(ts), ts, sizeof(uint64_t), vk::QueryResultFlagBits::e64);
		if (r == vk::Result::eSuccess) {
			uint64_t d = (ts[1] - ts[0]) & s.mask;
			uint64_t ns = (uint64_t)(d * s.periodNs);
			if (ns > 30000000ull)
				std::fprintf(stderr, "EASYCAST_GPUSLOW t_ms=%lld gpu_ms=%llu (frame of ~%u frames ago)\n",
						easycast_diag::tMs(), (unsigned long long)(ns / 1000000), SLOTS);
			s.sumNs += ns;
			if (ns > s.maxNs) s.maxNs = ns;
			s.count++;
			easycast_gpufreq::dev().frame(ns);		// 0.2.0: the emulator picks the GPU clock
		}
		s.written[slot] = false;
	}
	cmd.resetQueryPool(s.pool, slot * 2, 2);
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, s.pool, slot * 2);
	auto now = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.t0).count();
	if (ms >= 5000) {
		double busy = s.count ? 100.0 * (double)s.sumNs / ((double)ms * 1e6) : 0;
		std::fprintf(stderr, "EASYCAST_GPU interval_ms=%lld frames=%u avg_us=%llu max_us=%llu busy_pct=%.1f\n",
				(long long)ms, s.count, (unsigned long long)(s.count ? s.sumNs / s.count / 1000 : 0),
				(unsigned long long)(s.maxNs / 1000), busy);
		easycast_gpufreq::dev().report();
		s.sumNs = s.maxNs = 0;
		s.count = 0;
		s.t0 = now;
	}
}

// right before the frame's command buffer is ended
inline void end(vk::CommandBuffer cmd)
{
	State &s = st();
	if (s.disabled || !s.pool)
		return;
	uint32_t slot = s.frame % SLOTS;
	cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, s.pool, slot * 2 + 1);
	s.written[slot] = true;
	s.frame++;
}

} // namespace easycast_gpu
