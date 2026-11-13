// EasyCast 0.2.3 (Claude): read-ahead of the disc image. 14.11: single GD-ROM sector reads from the SD card took up
// to 2.3 s (Crazy Taxi, DOA2, Shenmue) while the emulation thread waited. A background thread now reads the next
// sectors after each request into a small cache (same Disc::ReadSectors code, serialized by one I/O mutex); a hit
// copies the data and restores the Q subchannel exactly as the direct read of that sector would have left it.
// Only the host's waiting time changes; data, subchannel and emulated GD-ROM timing stay the same.
// EASYCAST_DISC_PREFETCH=0 -> off. Journal: EASYCAST_DISC hits/misses/prefetched every ~10 s of reads.
// Included by imgread/common.cpp after the static q_subchannel definition.
#pragma once
#include "oslib/oslib.h"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace easycast_disc {

static constexpr u32 AHEAD = 256;		// sectors read ahead of the last request (~0.5 MB of 2048-byte sectors)
static constexpr u32 CHUNK = 4;		// sectors per background read (the I/O mutex is released in between)
static constexpr u32 MAX_CACHED = 2048;	// ~4-5 MB

struct Sector { std::vector<u8> data; u8 subq[96]; };

struct State {
	std::mutex io;						// Disc access (one FILE*), the cache and the read-ahead target
	std::condition_variable cv;
	std::thread worker;
	bool stop = false, enabled = false, started = false;
	Disc *owner = nullptr;
	u32 fmt = 0, from = 0, to = 0;		// read-ahead window [from, to) for format fmt
	std::unordered_map<u64, Sector> cache;
	std::deque<u64> order;
	u64 hits = 0, misses = 0, prefetched = 0, reqs = 0;
};
inline State &st() { static State *s = new State(); return *s; }	// never destroyed: the worker is detached
inline u64 key(u32 fad, u32 fmt) { return ((u64)fmt << 32) | fad; }

// caller holds st().io
inline void insert(State &s, u32 fad, u32 fmt, const u8 *data, const u8 *subq)
{
	u64 k = key(fad, fmt);
	if (s.cache.count(k))
		return;
	if (s.cache.size() >= MAX_CACHED) {
		s.cache.erase(s.order.front());
		s.order.pop_front();
	}
	Sector &sec = s.cache[k];
	sec.data.assign(data, data + fmt);
	std::memcpy(sec.subq, subq, 96);
	s.order.push_back(k);
}

inline void clearLocked(State &s)
{
	s.cache.clear();
	s.order.clear();
	s.from = s.to = 0;
	s.owner = nullptr;
}

inline void workerLoop()
{
	State &s = st();
	std::unique_lock<std::mutex> lk(s.io);
	std::vector<u8> buf(2448 * CHUNK);
	while (!s.stop)
	{
		s.cv.wait(lk, [&] { return s.stop || (s.owner != nullptr && s.from < s.to); });
		if (s.stop)
			break;
		Disc *d = s.owner;
		const u32 fmt = s.fmt, fad0 = s.from;
		u32 n = std::min(CHUNK, s.to - s.from);
		// skip sectors already cached
		while (n > 0 && s.cache.count(key(s.from, fmt))) {
			s.from++;
			n = std::min(CHUNK, s.to - s.from);
		}
		if (n == 0 || fad0 != s.from)
			continue;
		u8 subq[96];
		u32 got = 0;
		for (; got < n; got++) {
			bool miss = false;
			if (d->ReadSectors(fad0 + got, 1, &buf[got * fmt], fmt, true, nullptr, subq, &miss) != 1 || miss)
				break;		// end of disc or unreadable sector: let the emulation read it itself
			insert(s, fad0 + got, fmt, &buf[got * fmt], subq);
			s.prefetched++;
		}
		if (got < n)
			s.to = s.from;	// stop this window
		else
			s.from = fad0 + got;
		lk.unlock();		// let the emulation thread in between chunks
		std::this_thread::yield();
		lk.lock();
	}
}

inline bool on()
{
	State &s = st();
	if (!s.started) {
		s.started = true;
		const char *e = std::getenv("EASYCAST_DISC_PREFETCH");
		s.enabled = !(e != nullptr && std::strcmp(e, "0") == 0);
		if (s.enabled)
		{
			s.worker = std::thread([] { ThreadName _("Flycast-discra"); workerLoop(); });
			s.worker.detach();
		}
		std::fprintf(stderr, "EASYCAST_DISC prefetch=%d ahead=%u\n", s.enabled ? 1 : 0, AHEAD);
	}
	return s.enabled;
}

// replaces disc->ReadSectors(start, count, buff, fmt, stopOnMiss) for the emulation's GD-ROM reads
inline u32 read(Disc *d, u8 *buff, u32 start, u32 count, u32 fmt, bool stopOnMiss)
{
	if (!on() || stopOnMiss || fmt == 0 || fmt > 2448 || count == 0)
	{
		if (st().enabled) {
			std::lock_guard<std::mutex> lk(st().io);
			return d->ReadSectors(start, count, buff, fmt, stopOnMiss);
		}
		return d->ReadSectors(start, count, buff, fmt, stopOnMiss);
	}
	State &s = st();
	std::unique_lock<std::mutex> lk(s.io);
	if (s.owner != d) {
		clearLocked(s);
		s.owner = d;
	}
	for (u32 i = 0; i < count; ) {
		auto it = s.cache.find(key(start + i, fmt));
		if (it != s.cache.end()) {
			std::memcpy(buff + i * fmt, it->second.data.data(), fmt);
			std::memcpy(q_subchannel, it->second.subq, 96);		// as if this sector was just read
			s.hits++;
			i++;
			continue;
		}
		// run of misses: read it directly, exactly like before (updates q_subchannel itself)
		u32 j = i + 1;
		while (j < count && !s.cache.count(key(start + j, fmt))) j++;
		d->ReadSectors(start + i, j - i, buff + i * fmt, fmt, false);
		s.misses += j - i;
		i = j;
	}
	// next window after this request
	const u32 next = start + count;
	if (s.fmt != fmt || next < s.from || next > s.to + CHUNK) {
		s.fmt = fmt;
		s.from = next;
	}
	s.to = next + AHEAD;
	if (++s.reqs % 4096 == 0)
		std::fprintf(stderr, "EASYCAST_DISC hits=%llu misses=%llu prefetched=%llu\n", (unsigned long long)s.hits,
				(unsigned long long)s.misses, (unsigned long long)s.prefetched);
	lk.unlock();
	s.cv.notify_one();
	return count;
}

// before the disc object is deleted or replaced
inline void reset()
{
	State &s = st();
	if (!s.started)
		return;
	std::lock_guard<std::mutex> lk(s.io);
	clearLocked(s);
}

} // namespace easycast_disc
