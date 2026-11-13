#include "aica_thread.h"
#include "hw/arm7/arm7.h"
#include "hw/gdrom/gdrom_if.h"
#include "oslib/oslib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace aica::thread
{

static constexpr u64 MAX_LAG = 64;			// samples (~1.45 ms) before the SH4 computes the backlog itself
static constexpr u32 BATCH = 32;			// samples per mutex hold on the AICA thread
static constexpr int SLEEP_MIN_US = 100;	// idle back-off of the AICA thread
static constexpr int SLEEP_MAX_US = 2000;
static constexpr u32 CDDA_SAMPLES = 2352 / 2;

static std::atomic<bool> g_active { false };
static std::atomic<bool> g_stop { false };
static std::thread g_thread;
static std::mutex g_mutex;					// owns all AICA state while held
static std::atomic<u64> g_target { 0 };		// samples the SH4 has reached (written by SH4 only)
static std::atomic<u64> g_done { 0 };		// samples computed (written under g_mutex)
static thread_local bool t_onAica = false;
static thread_local int t_depth = 0;		// Sync nesting on this thread

// CD-DA one-sector prefetch (SH4 fills, AICA consumes)
static s16 g_cdda[CDDA_SAMPLES];
static std::atomic<bool> g_cddaFull { false };

// statistics, printed every ~5 s from the SH4 tick
static std::atomic<u64> st_thread { 0 }, st_helped { 0 }, st_syncs { 0 }, st_lagMax { 0 }, st_wakes { 0 };
static u32 st_ticks = 0;

bool active() { return g_active.load(std::memory_order_relaxed); }
bool onAicaThread() { return t_onAica; }

// under g_mutex
static u64 computeUpTo(u64 target, u32 limit)
{
	u64 done = g_done.load(std::memory_order_relaxed);
	u32 n = 0;
	while (done < target && n < limit)
	{
		arm::run(1);	// one sample: ARM7 slice + timeStep (timers, mixer/DSP, interrupts)
		done++;
		n++;
	}
	g_done.store(done, std::memory_order_release);
	return n;
}

// 0.1.7 "lazy" mode (EASYCAST_AICA_LAZY=0 -> 0.1.6 behaviour): the thread waits until LAZY_WAKE samples are
// pending and sleeps a fixed LAZY_SLEEP_US between checks -> ~3x fewer wake-ups and mutex round trips,
// same samples, same Sync catch-up. Pending stays well below MAX_LAG, so the SH4 rarely has to help.
// 0.2.0: the wait is computed from what is missing (one sample = ~22.7 us of real time) and the batch is 24 samples
// (~0.54 ms) -> about half the wake-ups of 0.1.7. EASYCAST_AICA_WAKE=<n> overrides (0.1.7 = 8 with a fixed 250 us).
static constexpr int SAMPLE_US = 22;
static u64 g_wake = 24;
static bool g_lazy = true;
static int lazySleepUs(u64 pending)
{
	int us = (int)(g_wake - pending) * SAMPLE_US;
	return std::min(std::max(us, SLEEP_MIN_US), 1000);
}

static void threadFunc()
{
	ThreadName _("Flycast-aica");
	t_onAica = true;
	t_depth = 1;
	int sleepUs = SLEEP_MIN_US;
	while (!g_stop.load(std::memory_order_acquire))
	{
		const u64 target = g_target.load(std::memory_order_acquire);
		const u64 done = g_done.load(std::memory_order_acquire);
		if (g_lazy && done < target && target - done < g_wake)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(lazySleepUs(target - done)));
			sleepUs = SLEEP_MIN_US;
			continue;
		}
		if (done < target)
		{
			u64 n;
			{
				std::lock_guard<std::mutex> lk(g_mutex);
				n = computeUpTo(g_target.load(std::memory_order_acquire), BATCH);
			}
			st_thread.fetch_add(n, std::memory_order_relaxed);
			st_wakes.fetch_add(1, std::memory_order_relaxed);
			sleepUs = SLEEP_MIN_US;
			continue;
		}
		if (g_lazy)
			sleepUs = std::max(sleepUs, lazySleepUs(0));
		std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
		sleepUs = std::min(sleepUs * 2, SLEEP_MAX_US);
	}
}

Sync::Sync()
{
	if (!active() || t_onAica || t_depth > 0)
	{
		t_depth++;
		return;
	}
	g_mutex.lock();
	locked = true;
	t_depth++;
	u64 n = computeUpTo(g_target.load(std::memory_order_acquire), ~0u);
	st_helped.fetch_add(n, std::memory_order_relaxed);
	st_syncs.fetch_add(1, std::memory_order_relaxed);
}

Sync::~Sync()
{
	t_depth--;
	if (locked)
		g_mutex.unlock();
}

void sh4Tick()
{
	const u64 target = g_target.load(std::memory_order_relaxed) + 1;
	g_target.store(target, std::memory_order_release);
	const u64 lag = target - g_done.load(std::memory_order_acquire);
	if (lag > st_lagMax.load(std::memory_order_relaxed))
		st_lagMax.store(lag, std::memory_order_relaxed);
	if (lag > MAX_LAG)
	{
		Sync s;		// the SH4 computes the backlog itself
	}
	if (!g_cddaFull.load(std::memory_order_acquire))
	{
		libCore_CDDA_Sector(g_cdda);
		g_cddaFull.store(true, std::memory_order_release);
	}
	if (++st_ticks >= 44100 * 5)
	{
		st_ticks = 0;
		std::fprintf(stderr, "EASYCAST_AICA thread=%llu helped=%llu syncs=%llu lag_max=%llu batches=%llu\n",
				(unsigned long long)st_thread.exchange(0), (unsigned long long)st_helped.exchange(0),
				(unsigned long long)st_syncs.exchange(0), (unsigned long long)st_lagMax.exchange(0),
				(unsigned long long)st_wakes.exchange(0));
	}
}

void cddaSector(s16 *sector)
{
	if (g_cddaFull.load(std::memory_order_acquire))
	{
		std::memcpy(sector, g_cdda, sizeof(g_cdda));
		g_cddaFull.store(false, std::memory_order_release);
	}
	else if (!t_onAica)
		libCore_CDDA_Sector(sector);		// SH4 thread computing the backlog: keep sector order
	else
		std::memset(sector, 0, sizeof(g_cdda));	// underrun (the SH4 refills every sample tick)
}

void resync()
{
	g_done.store(g_target.load(std::memory_order_acquire), std::memory_order_release);
	g_cddaFull.store(false, std::memory_order_release);
}

void start()
{
	stop();
	const char *env = std::getenv("EASYCAST_AICA_THREAD");
	if (env != nullptr && std::strcmp(env, "0") == 0)
	{
		std::fprintf(stderr, "EASYCAST_AICA threaded=0\n");
		return;
	}
	const char *lazy = std::getenv("EASYCAST_AICA_LAZY");
	g_lazy = !(lazy != nullptr && std::strcmp(lazy, "0") == 0);
	const char *wake = std::getenv("EASYCAST_AICA_WAKE");
	g_wake = 24;
	if (wake != nullptr && std::atoi(wake) >= 1 && std::atoi(wake) <= 48)
		g_wake = (u64)std::atoi(wake);
	g_stop.store(false);
	g_target.store(0);
	g_done.store(0);
	g_cddaFull.store(false);
	g_active.store(true, std::memory_order_release);
	g_thread = std::thread(threadFunc);
	std::fprintf(stderr, "EASYCAST_AICA threaded=1 max_lag=%llu lazy=%d wake=%llu\n", (unsigned long long)MAX_LAG,
			g_lazy ? 1 : 0, (unsigned long long)g_wake);
}

void stop()
{
	if (g_thread.joinable())
	{
		g_stop.store(true, std::memory_order_release);
		g_thread.join();
	}
	g_active.store(false, std::memory_order_release);
}

} // namespace aica::thread
