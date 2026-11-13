// EasyCast 0.2.4 (Claude): microfreeze catcher. EASYCAST_HITCH=<file> turns it on (test tile only).
// The frame timeline rings (easycast_timeline.h) then record always. At every screen present the render thread
// measures the time since the previous present; a gap > EASYCAST_HITCH_MS (default 34 ms = a frame held for 2+
// screen refreshes) that is also > 1.8x the median of the last 64 gaps (30 fps games) is a hitch:
//  - one line "EASYCAST_HITCH ..." on stderr: what both threads did inside the gap (summed from the rings) and the
//    counters (pipelines, textures, disc, audio push, longest emulation time slice, dropped presents);
//  - the raw events of [previous present - 60 ms, now] of both rings appended to <file> by a background thread.
// Every 5 s: "EASYCAST_FRAMES frames= gt20= gt34= gt50= gt100= drops=" (frame-time histogram).
#pragma once
#include "easycast_timeline.h"
#include "easycast_diag.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace easycast_hitch {

inline const char *file()
{
	static const char *p = [] {
		const char *e = std::getenv("EASYCAST_HITCH");
		return (e != nullptr && *e && std::strcmp(e, "0") != 0) ? e : nullptr;
	}();
	return p;
}
inline bool on() { return file() != nullptr; }

inline uint64_t thrNs()
{
	static const uint64_t v = [] {
		const char *e = std::getenv("EASYCAST_HITCH_MS");
		long ms = e != nullptr ? std::atol(e) : 0;
		return (uint64_t)(ms > 0 ? ms : 34) * 1000000ull;
	}();
	return v;
}

struct Sum {
	uint64_t emuWait = 0, enqWait = 0, vramWait = 0, vblMax = 0, idle = 0, proc = 0, draw = 0, pres = 0;
	unsigned vbl = 0, renders = 0, rtt = 0;
};

// events of one ring inside [a, b]; begin/end pairs are clipped to the window
inline void sumRing(int ri, uint64_t a, uint64_t b, Sum &s, std::vector<easycast_tl::Rec> *raw, uint64_t rawFrom)
{
	using namespace easycast_tl;
	Ring &R = ring(ri);
	uint32_t n = R.n.load(std::memory_order_acquire);
	uint32_t first = n > Ring::N - 64 ? n - (Ring::N - 64) : 0;	// keep clear of the slot being written
	uint32_t i = n;
	while (i > first && R.r[(i - 1) & (Ring::N - 1)].ns >= rawFrom)
		i--;
	uint64_t open[64] {};
	uint64_t lastVbl = 0;
	for (; i < n; i++) {
		const Rec x = R.r[i & (Ring::N - 1)];
		if (x.ns > b)
			break;
		if (raw != nullptr)
			raw->push_back(x);
		uint64_t t = std::max(x.ns, a);
		auto close = [&](uint32_t beginEv, uint64_t &acc) {
			uint64_t &o = open[beginEv & 63];
			if (o != 0 && x.ns >= a) acc += t - std::max(o, a);
			o = 0;
		};
		switch (x.ev) {
		case E_VBLANK:
			if (x.ns >= a) {
				s.vbl++;
				if (lastVbl != 0) s.vblMax = std::max(s.vblMax, x.ns - lastVbl);
			}
			lastVbl = x.ns;
			break;
		case E_WAIT_BEGIN: case E_ENQ_WAIT_BEGIN: case E_VRAM_WAIT_BEGIN: case R_IDLE_BEGIN:
		case R_PROCESS_BEGIN: case R_DRAW_BEGIN: case R_PRESENT_BEGIN:
			open[x.ev & 63] = x.ns;
			break;
		case E_WAIT_END: close(E_WAIT_BEGIN, s.emuWait); break;
		case E_ENQ_WAIT_END: close(E_ENQ_WAIT_BEGIN, s.enqWait); break;
		case E_VRAM_WAIT_END: close(E_VRAM_WAIT_BEGIN, s.vramWait); break;
		case R_IDLE_END: close(R_IDLE_BEGIN, s.idle); break;
		case R_PROCESS_END: close(R_PROCESS_BEGIN, s.proc); break;
		case R_DRAW_END: close(R_DRAW_BEGIN, s.draw); break;
		case R_PRESENT_END: close(R_PRESENT_BEGIN, s.pres); break;
		case R_RENDER_BEGIN: if (x.ns >= a) s.renders++; break;
		case R_RTT: if (x.ns >= a) s.rtt++; break;
		default: break;
		}
	}
	// still inside a wait at the end of the window (the emulation thread may be blocked right now)
	auto tail = [&](uint32_t beginEv, uint64_t &acc) {
		uint64_t o = open[beginEv & 63];
		if (o != 0) acc += b - std::max(o, a);
	};
	tail(E_WAIT_BEGIN, s.emuWait); tail(E_ENQ_WAIT_BEGIN, s.enqWait); tail(E_VRAM_WAIT_BEGIN, s.vramWait);
	tail(R_IDLE_BEGIN, s.idle);
	// time since the last vblank counts too: a stopped emulation thread shows as a long vblank gap
	if (lastVbl != 0 && b > lastVbl) s.vblMax = std::max(s.vblMax, b - lastVbl);
}

inline unsigned ms(uint64_t ns) { return (unsigned)((ns + 500000) / 1000000); }

// render thread, after every presented frame
inline void onPresent()
{
	if (!on())
		return;
	using easycast_diag::c;
	static uint64_t last = 0, t5 = 0, gaps[64] {};
	static unsigned ng = 0, frames = 0, gt20 = 0, gt34 = 0, gt50 = 0, gt100 = 0, hitches = 0;
	static easycast_diag::Delta dd;
	const uint64_t t = easycast_tl::now();
	if (t5 == 0) t5 = t;
	if (last != 0) {
		const uint64_t gap = t - last;
		frames++;
		gt20 += gap > 20000000ull; gt34 += gap > 34000000ull; gt50 += gap > 50000000ull; gt100 += gap > 100000000ull;
		uint64_t med = 0;
		if (gap > thrNs() && ng >= 16) {
			uint64_t tmp[64];
			unsigned k = std::min(ng, 64u);
			std::copy(gaps, gaps + k, tmp);
			std::nth_element(tmp, tmp + k / 2, tmp + k);
			med = tmp[k / 2];
		}
		easycast_diag::Delta x;
		dd.take(x);
		const uint64_t sliceMax = c().sliceMaxNs.exchange(0, std::memory_order_relaxed);
		const uint64_t audMax = c().audMaxNs.exchange(0, std::memory_order_relaxed);
		const uint64_t drops = c().presentDrops.exchange(0, std::memory_order_relaxed);
		if (med != 0 && gap * 10 > med * 18) {
			hitches++;
			Sum s;
			auto *raw = new std::vector<easycast_tl::Rec>[2];
			const uint64_t from = last > 60000000ull ? last - 60000000ull : 0;
			sumRing(0, last, t, s, &raw[0], from);
			sumRing(1, last, t, s, &raw[1], from);
			std::fprintf(stderr, "EASYCAST_HITCH n=%u t_ms=%lld gap_ms=%u med_ms=%u | emu: vbl=%u vbl_max_ms=%u "
					"rend_wait_ms=%u enq_wait_ms=%u vram_wait_ms=%u slice_max_ms=%u | rend: idle_ms=%u proc_ms=%u "
					"draw_ms=%u present_ms=%u renders=%u rtt=%u | pipes=%llu pipe_ms=%u tex=%llu tex_ms=%u disc=%llu "
					"disc_ms=%u aud_max_ms=%u drops=%llu\n", hitches, easycast_diag::tMs(), ms(gap), ms(med), s.vbl,
					ms(s.vblMax), ms(s.emuWait), ms(s.enqWait), ms(s.vramWait), ms(sliceMax), ms(s.idle), ms(s.proc),
					ms(s.draw), ms(s.pres), s.renders, s.rtt, (unsigned long long)x.pipes, ms(x.pipeNs),
					(unsigned long long)x.texs, ms(x.texNs), (unsigned long long)x.discReads, ms(x.discNs), ms(audMax),
					(unsigned long long)drops);
			// raw events: written off the render thread (the SD card can block for a long time)
			const unsigned n = hitches;
			const long long tms = easycast_diag::tMs();
			const uint64_t g = gap, base = last;
			std::thread([raw, n, tms, g, base] {
				if (FILE *f = std::fopen(file(), "a")) {
					std::fprintf(f, "# hitch %u t_ms=%lld gap_ms=%u (times in us from the previous present)\n", n, tms, ms(g));
					for (int ri = 0; ri < 2; ri++)
						for (const auto &e : raw[ri])
							std::fprintf(f, "%c %s %lld\n", ri ? 'R' : 'E', easycast_tl::name(e.ev),
									((long long)e.ns - (long long)base) / 1000);
					std::fclose(f);
				}
				delete[] raw;
			}).detach();
		}
		gaps[ng++ % 64] = gap;
	}
	last = t;
	if (t - t5 >= 5000000000ull) {
		std::fprintf(stderr, "EASYCAST_FRAMES t_ms=%lld frames=%u gt20=%u gt34=%u gt50=%u gt100=%u hitches=%u\n",
				easycast_diag::tMs(), frames, gt20, gt34, gt50, gt100, hitches);
		frames = gt20 = gt34 = gt50 = gt100 = 0;
		t5 = t;
	}
}

} // namespace easycast_hitch
