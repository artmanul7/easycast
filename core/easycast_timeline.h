// EasyCast (Claude) frame timeline: per-thread lock-free rings of timestamped events.
// Enabled only when EASYCAST_TIMELINE=<output file> is set. The render thread checks once per
// 30 presents whether the trigger file <output>.dump exists and then writes both rings once.
// Output lines: "<ring> <event> <monotonic_ns>"; ring E = emulator thread, R = render thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

namespace easycast_tl {

enum Ev : uint32_t {
	// emulator thread
	E_VBLANK = 1, E_START_RENDER, E_QUEUED, E_WAIT_BEGIN, E_WAIT_END, E_ENQ_WAIT_BEGIN, E_ENQ_WAIT_END,
	E_VRAM_WAIT_BEGIN, E_VRAM_WAIT_END, E_RENDER_DONE_CB,
	// render thread
	R_IDLE_BEGIN = 32, R_IDLE_END, R_RENDER_BEGIN, R_PROCESS_BEGIN, R_PROCESS_END, R_RENDEREND_SET,
	R_DRAW_BEGIN, R_DRAW_END, R_PRESENT_BEGIN, R_PRESENT_END, R_RENDER_END, R_RTT,
};

static const char *name(uint32_t e)
{
	switch (e) {
	case E_VBLANK: return "vblank"; case E_START_RENDER: return "start_render"; case E_QUEUED: return "queued";
	case E_WAIT_BEGIN: return "wait_begin"; case E_WAIT_END: return "wait_end";
	case E_ENQ_WAIT_BEGIN: return "enq_wait_begin"; case E_ENQ_WAIT_END: return "enq_wait_end";
	case E_VRAM_WAIT_BEGIN: return "vram_wait_begin"; case E_VRAM_WAIT_END: return "vram_wait_end";
	case E_RENDER_DONE_CB: return "render_done_cb";
	case R_IDLE_BEGIN: return "idle_begin"; case R_IDLE_END: return "idle_end";
	case R_RENDER_BEGIN: return "render_begin"; case R_PROCESS_BEGIN: return "process_begin";
	case R_PROCESS_END: return "process_end"; case R_RENDEREND_SET: return "renderend_set";
	case R_DRAW_BEGIN: return "draw_begin"; case R_DRAW_END: return "draw_end";
	case R_PRESENT_BEGIN: return "present_begin"; case R_PRESENT_END: return "present_end";
	case R_RENDER_END: return "render_end"; case R_RTT: return "rtt";
	default: return "?";
	}
}

struct Rec { uint64_t ns; uint32_t ev; uint32_t pad; };
struct Ring {
	static constexpr uint32_t N = 1u << 16;
	Rec r[N];
	std::atomic<uint32_t> n { 0 };
};

inline Ring &ring(int i) { static Ring rings[2]; return rings[i]; }

inline const char *outPath()
{
	static const char *p = std::getenv("EASYCAST_TIMELINE");
	return p && *p ? p : nullptr;
}

inline uint64_t now()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ring 0 = emulator thread, ring 1 = render thread; each ring has a single writer thread
inline void mark(int ri, uint32_t ev)
{
	static const bool rec = outPath() != nullptr || [] {	// 0.2.4: EASYCAST_HITCH needs the rings too
		const char *e = std::getenv("EASYCAST_HITCH");
		return e != nullptr && *e && !(e[0] == '0' && e[1] == 0);
	}();
	if (!rec)
		return;
	Ring &R = ring(ri);
	uint32_t i = R.n.load(std::memory_order_relaxed);
	R.r[i & (Ring::N - 1)] = { now(), ev, 0 };
	R.n.store(i + 1, std::memory_order_release);
}

inline void maybeDump()
{
	const char *out = outPath();
	if (!out)
		return;
	static bool done = false;
	static uint32_t calls = 0;
	if (done || (++calls % 30) != 0)
		return;
	char trig[512];
	std::snprintf(trig, sizeof trig, "%s.dump", out);
	if (access(trig, F_OK) != 0)
		return;
	done = true;
	char tmp[512];
	std::snprintf(tmp, sizeof tmp, "%s.tmp", out);
	FILE *f = std::fopen(tmp, "w");
	if (!f)
		return;
	std::fprintf(f, "# easycast timeline v1 now_ns=%llu\n", (unsigned long long)now());
	for (int ri = 0; ri < 2; ri++) {
		Ring &R = ring(ri);
		uint32_t n = R.n.load(std::memory_order_acquire);
		uint32_t first = n > Ring::N ? n - Ring::N : 0;
		for (uint32_t i = first; i < n; i++) {
			const Rec &x = R.r[i & (Ring::N - 1)];
			std::fprintf(f, "%c %s %llu\n", ri == 0 ? 'E' : 'R', name(x.ev), (unsigned long long)x.ns);
		}
	}
	std::fclose(f);
	std::rename(tmp, out);
	std::fprintf(stderr, "EASYCAST_TIMELINE dumped %s\n", out);
}

} // namespace easycast_tl
