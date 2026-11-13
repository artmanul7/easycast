// EasyCast 0.2.0 (Claude): the emulator chooses the GPU clock itself. The Mali kbase devfreq driver reports no load,
// so the kernel's simple_ondemand stays at 800 MHz; we already measure the GPU time of every screen frame.
// Every WINDOW measured frames: GPU busy share of a 60 Hz frame (16.7 ms) and the render thread's GPU fence wait.
//  - busy > 80 % of the budget, a frame > 95 %, or the render thread waits for the GPU > 1 ms/frame -> top clock now
//  - the next lower step would still keep busy <= 70 % and the slowest frame <= 85 % for 2 windows -> one step down
// The clock is set through max_freq of the GPU devfreq device (the launcher keeps governor "performance", so the GPU
// runs at max_freq). Only screen passes are timed (not render-to-texture, not the final copy): the 70 % / 85 %
// margins and the fence-wait rule cover the rest. EASYCAST_GPU_DVFS=0 -> off (fixed top clock as before).
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "easycast_diag.h"

namespace easycast_gpufreq {

constexpr uint32_t WINDOW = 30;					// measured frames per decision (~0.5 s at 60 fps)
constexpr double BUDGET_NS = 1e9 / 60.0;
constexpr double UP_AVG = 0.80, UP_PEAK = 0.95, DOWN_AVG = 0.70, DOWN_PEAK = 0.85;
constexpr double UP_FENCE_NS = 1.0e6;
constexpr int DOWN_WINDOWS = 3;
// after a forced jump to the top no step down for HOLD windows; a jump soon after a step down (the lower step was
// too slow) doubles the hold, up to HOLD_MAX (~60 s): no ping-pong between clocks (14.11: Crazy Taxi at 400 MHz)
constexpr int HOLD_MIN = 6, HOLD_MAX = 120, RECENT_DOWN = 10;

// pure decision logic (host-testable)
struct Controller {
	std::vector<uint64_t> freqs;	// Hz, ascending
	int idx = -1;					// current step
	uint64_t sumNs = 0, maxNs = 0, fenceNs = 0;
	uint32_t frames = 0;
	int lowWindows = 0;
	uint32_t changes = 0;
	int hold = 0, holdLen = HOLD_MIN, sinceDown = 1000;

	int top() const { return (int)freqs.size() - 1; }

	// one measured frame; returns the new step if it changes, else -1
	int frame(uint64_t gpuNs)
	{
		sumNs += gpuNs;
		maxNs = std::max(maxNs, gpuNs);
		if (++frames < WINDOW)
			return -1;
		const double avg = (double)sumNs / frames, peak = (double)maxNs, fence = (double)fenceNs / frames;
		sumNs = maxNs = fenceNs = 0;
		frames = 0;
		int next = idx;
		sinceDown++;
		if (hold > 0)
			hold--;
		if (avg > UP_AVG * BUDGET_NS || peak > UP_PEAK * BUDGET_NS || fence > UP_FENCE_NS) {
			next = top();
			lowWindows = 0;
			if (idx != top()) {
				holdLen = sinceDown <= RECENT_DOWN ? std::min(holdLen * 2, HOLD_MAX) : HOLD_MIN;
				hold = holdLen;
			}
		} else if (idx > 0 && hold == 0) {
			const double scale = (double)freqs[idx] / (double)freqs[idx - 1];
			if (avg * scale <= DOWN_AVG * BUDGET_NS && peak * scale <= DOWN_PEAK * BUDGET_NS) {
				if (++lowWindows >= DOWN_WINDOWS) {
					next = idx - 1;
					lowWindows = 0;
					sinceDown = 0;
				}
			} else {
				lowWindows = 0;
			}
		}
		if (next == idx)
			return -1;
		idx = next;
		changes++;
		return idx;
	}
};

#if defined(__linux__) && !defined(EASYCAST_GPUFREQ_HOST_TEST)
static const char *DEV = "/sys/class/devfreq/fde60000.gpu/";

inline std::string readFile(const std::string &p)
{
	std::string out;
	if (FILE *f = std::fopen(p.c_str(), "r")) {
		char buf[512];
		size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = 0;
		out = buf;
		std::fclose(f);
	}
	return out;
}

inline bool writeFile(const std::string &p, uint64_t v)
{
	FILE *f = std::fopen(p.c_str(), "w");
	if (!f) return false;
	bool ok = std::fprintf(f, "%llu", (unsigned long long)v) > 0;
	ok = (std::fclose(f) == 0) && ok;
	return ok;
}

struct Device {
	Controller c;
	bool ready = false, failed = false;
	std::atomic<uint64_t> fenceNs { 0 };		// added by the render thread's CommandPool fence waits
	uint64_t shareFrames[16] {};				// measured frames per step since the last report

	bool init()
	{
		if (ready || failed) return ready;
		const char *e = std::getenv("EASYCAST_GPU_DVFS");
		if (e != nullptr && std::strcmp(e, "0") == 0) {
			std::fprintf(stderr, "EASYCAST_GPUFREQ off\n");
			failed = true;
			return false;
		}
		std::string s = readFile(std::string(DEV) + "available_frequencies");
		for (const char *p = s.c_str(); *p; ) {
			char *q;
			unsigned long long v = std::strtoull(p, &q, 10);
			if (q == p) { p++; continue; }
			if (v > 0) c.freqs.push_back(v);
			p = q;
		}
		std::sort(c.freqs.begin(), c.freqs.end());
		if (c.freqs.size() < 2 || c.freqs.size() > 16 || !writeFile(std::string(DEV) + "max_freq", c.freqs.back())) {
			std::fprintf(stderr, "EASYCAST_GPUFREQ unavailable (%zu steps)\n", c.freqs.size());
			failed = true;
			return false;
		}
		c.idx = c.top();
		ready = true;
		std::fprintf(stderr, "EASYCAST_GPUFREQ on steps=%zu top_mhz=%llu\n", c.freqs.size(),
				(unsigned long long)(c.freqs.back() / 1000000));
		return true;
	}

	void frame(uint64_t gpuNs)
	{
		if (!init()) return;
		c.fenceNs += fenceNs.exchange(0, std::memory_order_relaxed);
		shareFrames[c.idx]++;
		int n = c.frame(gpuNs);
		if (n >= 0) {
			lastChangeMhz = c.freqs[n] / 1000000;
			lastChange = std::chrono::steady_clock::now();
			const uint64_t w0 = easycast_diag::nowNs();
			if (!writeFile(std::string(DEV) + "max_freq", c.freqs[n]))
				std::fprintf(stderr, "EASYCAST_GPUFREQ write failed\n");
			std::fprintf(stderr, "EASYCAST_GPUFREQ set t_ms=%lld mhz=%llu write_us=%llu\n", easycast_diag::tMs(),
					(unsigned long long)lastChangeMhz, (unsigned long long)((easycast_diag::nowNs() - w0) / 1000));
		}
	}
	std::chrono::steady_clock::time_point lastChange {};
	uint64_t lastChangeMhz = 0;

	// one line every ~5 s from the GPU timer report
	void report()
	{
		if (!ready) return;
		std::string shares;
		uint64_t tot = 0;
		for (size_t i = 0; i < c.freqs.size(); i++) tot += shareFrames[i];
		for (size_t i = 0; i < c.freqs.size(); i++) {
			char b[32];
			std::snprintf(b, sizeof(b), "%s%llu:%llu", i ? "," : "", (unsigned long long)(c.freqs[i] / 1000000),
					(unsigned long long)(tot ? shareFrames[i] * 100 / tot : 0));
			shares += b;
			shareFrames[i] = 0;
		}
		std::string cur = readFile(std::string(DEV) + "cur_freq");
		while (!cur.empty() && (cur.back() == '\n' || cur.back() == ' ')) cur.pop_back();
		std::fprintf(stderr, "EASYCAST_GPUFREQ cap_mhz=%llu changes=%u share_pct=%s cur=%s\n",
				(unsigned long long)(c.freqs[c.idx] / 1000000), c.changes, shares.c_str(), cur.c_str());
		c.changes = 0;
	}

	~Device()
	{
		if (ready) writeFile(std::string(DEV) + "max_freq", c.freqs.back());	// leave the GPU as we found it
	}
};

inline Device &dev() { static Device d; return d; }
#endif

} // namespace easycast_gpufreq
