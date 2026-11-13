// EasyCast (Claude): fast-forward of a Dreamcast SDK wait loop (found in Dead or Alive 2 and GTA 2).
//
// The loop (SH4, any address):            head+0   jsr @R13 / mov #7,R4      call table slot 7 (an empty function)
//                                          head+4   counter--  (*[R15])        timeout counter at R15+8
//                                                   if (*[R15+4] != R14) exit  polled variable changed (interrupt handler)
//                                          head+20  if (counter == 0) exit; loop
// While the game spins here, nothing but the scheduler (events -> interrupts) can end the loop. Like Flycast's own
// SLEEP opcode, we then run further 448-cycle time slices without executing the loop and decrement the timeout
// counter by exactly the iterations those cycles would have taken. Interrupts are still taken at slice boundaries,
// as before; only the position inside one ~50-cycle iteration can differ. EASYCAST_IDLE=0 -> off.
#pragma once
#include "types.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/modules/mmu.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/mem/addrspace.h"
#include "easycast_diag.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace easycast_idle {

static constexpr u16 LOOP_CODE[] = { 0x4D0B, 0xE407, 0x63F2, 0x6232, 0x72FF, 0x2322, 0x53F1, 0x6232,
								0x32E0, 0x8B03, 0x61F2, 0x6312, 0x2338, 0x8BF1 };
static constexpr u16 CALLEE_CODE[] = { 0x4F22, 0x7FF8, 0x6243, 0xD30E, 0x4208, 0x2F42, 0x4200, 0x323C,
								  0x6423, 0x1F21, 0x6122, 0x410B, 0x5441 };
static constexpr int MAX_SLICES = 2048;		// per call (~0.9 M cycles), keeps the main loop responsive

struct Stats { u64 calls = 0, ffCalls = 0, slices = 0, iters = 0; u32 lastHead = 0; };
inline Stats &stats() { static Stats s; return s; }

inline bool enabled()
{
	static const bool on = [] {
		const char *e = std::getenv("EASYCAST_IDLE");
		bool v = !(e != nullptr && std::strcmp(e, "0") == 0);
		std::fprintf(stderr, "EASYCAST_IDLE enabled=%d\n", v ? 1 : 0);
		return v;
	}();
	return on;
}

inline bool isRam(u32 a) { return (a & 0x1C000000) == 0x0C000000; }

inline bool match(u32 addr, const u16 *pat, size_t n)
{
	if (!isRam(addr) || !isRam(addr + 2 * n))
		return false;
	for (size_t i = 0; i < n; i++)
		if (addrspace::read16(addr + 2 * i) != pat[i])
			return false;
	return true;
}

// pc of one of the loop's three blocks -> loop head, 0 if pc is not in such a loop
inline u32 loopHead(u32 pc)
{
	if (!isRam(pc) || (pc & 1))
		return 0;
	u16 op = addrspace::read16(pc);
	u32 head = op == LOOP_CODE[0] ? pc : op == LOOP_CODE[2] ? pc - 4 : op == LOOP_CODE[10] ? pc - 20 : 0;
	return head != 0 && match(head, LOOP_CODE, sizeof(LOOP_CODE) / sizeof(LOOP_CODE[0])) ? head : 0;
}

// callee (R13) must be the table-call helper and table slot 7 an empty function (rts / nop)
inline bool calleeIsIdle(u32 callee, u32 &func)
{
	if (!match(callee, CALLEE_CODE, sizeof(CALLEE_CODE) / sizeof(CALLEE_CODE[0])))
		return false;
	u32 lit = ((callee + 6) & ~3u) + 4 + 0x0E * 4;		// mov.l @(0x0E,PC),R3 at callee+6
	u32 table = addrspace::read32(lit);
	if (!isRam(table))
		return false;
	func = addrspace::read32(table + 7 * 8);
	return isRam(func) && addrspace::read16(func) == 0x000B && addrspace::read16(func + 2) == 0x0009;
}

inline u32 blockCycles(u32 addr)
{
	RuntimeBlockInfoPtr b = bm_GetBlock(addr);
	return b ? b->guest_cycles : 0;
}

// cycles of one iteration as Flycast counts them (sum of the compiled blocks); 0 if a block is not compiled yet
inline u32 iterationCycles(u32 head, u32 callee, u32 func)
{
	static u32 cHead = 0, cCallee = 0, cFunc = 0, cCycles = 0;
	if (head == cHead && callee == cCallee && func == cFunc && cCycles != 0)
		return cCycles;
	u32 parts[] = { blockCycles(head), blockCycles(head + 4), blockCycles(head + 20),
					blockCycles(callee), blockCycles(callee + 0x1A), blockCycles(func) };
	u32 sum = 0;
	for (u32 p : parts) {
		if (p == 0)
			return 0;
		sum += p;
	}
	cHead = head; cCallee = callee; cFunc = func; cCycles = sum;
	return sum;
}

// ---- kind 2 (0.2.0): Dreamcast SDK frame wait, byte-identical in Crazy Taxi, Sonic Adventure 2, Jet Set Radio,
// Shenmue. head+0: callback(arg) from a {func, arg} struct (must be rts/nop); head+8: exit if c > a + b + 1;
// head+0x20: loop while d != 0. a, b, c, d are RAM words that only interrupt handlers change; the loop writes nothing.
static constexpr u16 WAIT2_CODE[] = { 0xD321, 0x6232, 0x420B, 0x5431, 0xD120, 0x6312, 0xD020, 0x6202, 0xD11B, 0x323C,
									  0x7201, 0x6312, 0x3326, 0x8B01, 0xA004, 0x6CE3, 0xD318, 0x6232, 0x2228, 0x8BEB };

inline u32 pcLiteral(u32 pc) { return ((pc & ~3u) + 4 + (addrspace::read16(pc) & 0xFF) * 4); }

inline u32 wait2Head(u32 pc)
{
	if (!isRam(pc) || (pc & 1))
		return 0;
	u16 op = addrspace::read16(pc);
	u32 head = op == WAIT2_CODE[0] ? pc : op == WAIT2_CODE[4] ? pc - 8 : op == WAIT2_CODE[16] ? pc - 0x20 : 0;
	return head != 0 && match(head, WAIT2_CODE, sizeof(WAIT2_CODE) / sizeof(WAIT2_CODE[0])) ? head : 0;
}

struct Wait2 { u32 a, b, c, d; };
inline bool wait2Setup(u32 head, Wait2 &w)
{
	u32 cb = addrspace::read32(pcLiteral(head));
	if (!isRam(cb))
		return false;
	u32 func = addrspace::read32(cb);
	if (!isRam(func) || addrspace::read16(func) != 0x000B || addrspace::read16(func + 2) != 0x0009)
		return false;
	w.a = addrspace::read32(pcLiteral(head + 8));
	w.b = addrspace::read32(pcLiteral(head + 0xC));
	w.c = addrspace::read32(pcLiteral(head + 0x10));
	w.d = addrspace::read32(pcLiteral(head + 0x20));
	return isRam(w.a) && isRam(w.b) && isRam(w.c) && isRam(w.d);
}
// true while the game would keep looping
inline bool wait2Spins(const Wait2 &w)
{
	u32 limit = addrspace::read32(w.a) + addrspace::read32(w.b) + 1;
	return !(addrspace::read32(w.c) > limit) && addrspace::read32(w.d) != 0;
}

// ---- kind 3 (0.2.0): one-block spin "mov.l @Rm,Rn ; cmp/xx or tst with Rn ; bt/bf back to itself" on a RAM word
// (Shenmue: wait until a counter reaches a target). Only the loaded register changes; nothing is written.
struct Spin { u32 addr; u16 cmp; bool bt; int n, k; };
inline bool spinSetup(u32 pc, Spin &s)
{
	if (!isRam(pc) || (pc & 1))
		return false;
	u16 op1 = addrspace::read16(pc), op2 = addrspace::read16(pc + 2), op3 = addrspace::read16(pc + 4);
	if ((op1 & 0xF00F) != 0x6002 || (op3 != 0x89FC && op3 != 0x8BFC))
		return false;
	const int n = (op1 >> 8) & 15, m = (op1 >> 4) & 15;
	const int kind = op2 & 0xF00F;
	const int x = (op2 >> 8) & 15, y = (op2 >> 4) & 15;
	const bool cmp = (op2 & 0xF000) == 0x3000 && (kind == 0x3000 || kind == 0x3002 || kind == 0x3003 || kind == 0x3006 || kind == 0x3007);
	const bool tst = kind == 0x2008;
	if (!(cmp || tst) || (x != n && y != n) || m == n)
		return false;
	s.addr = Sh4cntx.r[m];
	if (!isRam(s.addr) || (s.addr & 3))
		return false;
	s.cmp = op2;
	s.bt = op3 == 0x89FC;
	s.n = n;
	s.k = 0;
	return true;
}
inline bool spinSpins(const Spin &s)
{
	u32 r[16];
	std::memcpy(r, Sh4cntx.r, sizeof(r));
	r[s.n] = addrspace::read32(s.addr);
	const u32 rn = r[(s.cmp >> 8) & 15], rm = r[(s.cmp >> 4) & 15];		// SH4: op Rm,Rn
	bool T;
	switch (s.cmp & 0xF00F) {
	case 0x3000: T = rn == rm; break;
	case 0x3002: T = rn >= rm; break;
	case 0x3003: T = (s32)rn >= (s32)rm; break;
	case 0x3006: T = rn > rm; break;
	case 0x3007: T = (s32)rn > (s32)rm; break;
	default: T = (rn & rm) == 0; break;		// tst
	}
	return s.bt ? T : !T;
}

// ---- kind 1 (0.1.9): returns slices skipped, -1 if pc is not in that loop
inline int skipKind1(int &r)
{
	const u32 head = loopHead(Sh4cntx.pc);
	if (head == 0)
		return -1;
	const u32 r15 = Sh4cntx.r[15];
	u32 func;
	if (!isRam(r15) || addrspace::read32(r15) != r15 + 8 || !calleeIsIdle(Sh4cntx.r[13], func))
		return 0;
	const u32 var = addrspace::read32(r15 + 4);
	const u32 expected = Sh4cntx.r[14];
	if (!isRam(var) || addrspace::read32(var) != expected)
		return 0;
	const u32 iter = iterationCycles(head, Sh4cntx.r[13], func);
	if (iter == 0)
		return 0;
	static u32 carry = 0;		// cycles of a started iteration, carried between calls
	int n = 0;
	while (n < MAX_SLICES)
	{
		const u32 cnt = addrspace::read32(r15 + 8);
		const u32 total = carry + SH4_TIMESLICE;
		const u32 dec = total / iter;
		if (cnt <= dec + 1)
			break;		// the timeout would end the loop: let the game run it
		addrspace::write32(r15 + 8, cnt - dec);
		carry = total % iter;
		stats().iters += dec;
		n++;
		r = UpdateSystem_INTC();
		if (r != 0 || addrspace::read32(var) != expected)
			break;
	}
	if (n > 0) stats().lastHead = head;
	return n;
}

template<typename Spins>
inline int skipWhile(int &r, Spins spins)
{
	int n = 0;
	while (n < MAX_SLICES && spins())
	{
		n++;
		r = UpdateSystem_INTC();
		if (r != 0)
			break;
	}
	return n;
}

// 14.11: the pattern tests above read 3-5 guest halfwords on EVERY time slice (~446 000/s, ~2-3% of the emulation
// thread in DOA2). A pc whose code matches none of the three loop kinds is remembered here and not tested again;
// the table is cleared every ~5 s (new code may be loaded at the same address). A miss only skips a skip: the
// emulation itself is never changed by this cache.
static constexpr u32 NEG_SIZE = 4096;
inline u32 *negPcs() { static u32 t[NEG_SIZE]; return t; }
inline bool negHit(u32 pc) { return negPcs()[(pc >> 1) & (NEG_SIZE - 1)] == pc; }
inline void negRemember(u32 pc) { negPcs()[(pc >> 1) & (NEG_SIZE - 1)] = pc; }
inline void negClear() { std::memset(negPcs(), 0, NEG_SIZE * sizeof(u32)); }

// code part of spinSetup (no register values): false = this pc can never be a kind-3 spin
inline bool spinCode(u32 pc)
{
	if (!isRam(pc) || (pc & 1))
		return false;
	const u16 op1 = addrspace::read16(pc), op2 = addrspace::read16(pc + 2), op3 = addrspace::read16(pc + 4);
	if ((op1 & 0xF00F) != 0x6002 || (op3 != 0x89FC && op3 != 0x8BFC))
		return false;
	const int n = (op1 >> 8) & 15, m = (op1 >> 4) & 15;
	const int kind = op2 & 0xF00F;
	const int x = (op2 >> 8) & 15, y = (op2 >> 4) & 15;
	const bool cmp = (op2 & 0xF000) == 0x3000 && (kind == 0x3000 || kind == 0x3002 || kind == 0x3003 || kind == 0x3006 || kind == 0x3007);
	const bool tst = kind == 0x2008;
	return (cmp || tst) && (x == n || y == n) && m != n;
}

struct KindStats { u64 calls = 0, slices = 0; };
inline KindStats *kindStats() { static KindStats k[4]; return k; }

// replaces UpdateSystem_INTC() in the arm64 dynarec time-slice stub; Sh4cntx.pc = block about to run
inline int UpdateSystem()
{
	easycast_diag::emuSlice();
	int r = UpdateSystem_INTC();
	if (r != 0 || !enabled() || mmu_enabled())
		return r;
	Stats &s = stats();
	KindStats *k = kindStats();
	if (++s.calls + s.slices >= 446000ull * 5) {		// ~5 s of guest time (normal + skipped slices)
		std::fprintf(stderr, "EASYCAST_IDLE ff_calls=%llu slices=%llu iters=%llu head=%08X k1=%llu/%llu k2=%llu/%llu k3=%llu/%llu\n",
				(unsigned long long)s.ffCalls, (unsigned long long)s.slices, (unsigned long long)s.iters, s.lastHead,
				(unsigned long long)k[1].calls, (unsigned long long)k[1].slices, (unsigned long long)k[2].calls,
				(unsigned long long)k[2].slices, (unsigned long long)k[3].calls, (unsigned long long)k[3].slices);
		s = Stats();
		for (int i = 0; i < 4; i++) k[i] = KindStats();
		negClear();
	}
	const u32 pc = Sh4cntx.pc;
	if (negHit(pc))
		return r;
	int kind = 1;
	int n = skipKind1(r);
	if (n < 0) {
		n = 0;
		Wait2 w;
		Spin sp;
		if (u32 head = wait2Head(pc)) {
			kind = 2;
			if (wait2Setup(head, w)) {
				n = skipWhile(r, [&] { return wait2Spins(w); });
				if (n > 0) s.lastHead = head;
			}
		} else if (!spinCode(pc)) {
			negRemember(pc);		// none of the three loop patterns is at this pc
		} else if (spinSetup(pc, sp)) {
			kind = 3;
			n = skipWhile(r, [&] { return spinSpins(sp); });
			if (n > 0) s.lastHead = pc;
		}
	}
	if (n > 0) {
		s.ffCalls++;
		s.slices += n;
		k[kind].calls++;
		k[kind].slices += n;
	}
	return r;
}

} // namespace easycast_idle
