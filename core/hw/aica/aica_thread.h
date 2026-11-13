/*
	EasyCast 0.1.6: AICA (ARM7 + mixer + DSP) on its own thread.

	The SH4 scheduler still fires AicaUpdate every AICA_TICK cycles; in threaded mode it only counts
	the sample (target) and the AICA thread computes it shortly after. The AICA thread never runs ahead
	of the SH4: it computes samples [done, target).

	Every SH4 access to AICA state goes through a Sync guard: it takes the AICA mutex and first computes
	the samples still missing (done -> target) on the SH4 thread, so the SH4 sees exactly the state the
	single-threaded emulator would show at that point. AICA -> SH4 interrupts are evaluated on the SH4
	thread at every sample tick (latency of one or two samples instead of zero). If the AICA thread falls
	behind by more than MAX_LAG samples, the SH4 computes the backlog itself.
	ARAM stays directly mapped in the SH4 address space and is not synchronised (as on hardware).
	CD-DA sectors are read on the SH4 thread one sector ahead (the GD-ROM state belongs to the SH4).

	EASYCAST_AICA_THREAD=0 keeps the original single-threaded path.
*/
#pragma once
#include "types.h"

namespace aica::thread
{

void start();				// aica::init
void stop();				// aica::term
bool active();				// threaded mode running
bool onAicaThread();		// true on the AICA worker thread

// SH4 side, from AicaUpdate (threaded mode only): count the sample, refill CD-DA, bound the lag
void sh4Tick();

// After deserialize / reset (call inside a Sync): forget the backlog
void resync();

// CD-DA source for sgc::AICA_Sample in threaded mode
void cddaSector(s16 *sector);

// RAII: exclusive, caught-up access to AICA state from the SH4 (or any non-AICA) thread.
// Nested and AICA-thread uses are no-ops.
class Sync
{
public:
	Sync();
	~Sync();
	Sync(const Sync&) = delete;
	Sync& operator=(const Sync&) = delete;
private:
	bool locked = false;
};

} // namespace aica::thread
