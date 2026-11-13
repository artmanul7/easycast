// EasyCast 0.2.3 (Claude): pre-built GPU pipelines. A Vulkan pipeline is created the first time a polygon state
// (blending, fog, culling, texture format...) appears; on Mali that costs ~4 ms each and a new effect can need 20+
// at once (14.11 Soul Calibur per-pixel: 21 pipelines = 87 ms -> a visible hitch). Every pipeline the game needed is
// now remembered in data/easycast-pipes-<game id>-<tri|oit>.txt (only the polygon words that select a pipeline), and
// at the first pipeline miss of a game session all remembered ones are built at once - during the game's start-up
// instead of in the middle of a fight. Same pipelines, same hashes. EASYCAST_PIPE_CACHE=0 -> off.
#pragma once
#include "types.h"
#include "stdclass.h"
#include "hw/pvr/ta_ctx.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

namespace easycast_pipes {

struct Rec { u32 listType, sort, pass, gpuPalette, dithering, tsp, tcw, pcw, isp, tileclip, tsp1, tcw1; };

inline bool on()
{
	static const bool v = [] {
		const char *e = std::getenv("EASYCAST_PIPE_CACHE");
		return !(e != nullptr && std::strcmp(e, "0") == 0);
	}();
	return v;
}

inline std::string fileFor(int kind)
{
	const std::string &id = settings.content.gameId;
	if (id.empty())
		return "";
	std::string safe;
	for (char c : id)
		safe += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
	return get_writable_data_path("easycast-pipes-" + safe + (kind ? "-oit" : "-tri") + ".txt");
}

inline std::string line(const Rec &r)
{
	char b[160];
	std::snprintf(b, sizeof(b), "%x %x %x %x %x %08x %08x %08x %08x %08x %08x %08x", r.listType, r.sort, r.pass,
			r.gpuPalette, r.dithering, r.tsp, r.tcw, r.pcw, r.isp, r.tileclip, r.tsp1, r.tcw1);
	return b;
}

struct KindState { const void *manager = nullptr; std::string game; std::set<std::string> known; };
inline KindState &ks(int kind) { static KindState k[2]; return k[kind]; }

inline Rec fromPoly(u32 listType, u32 sort, u32 pass, u32 gpuPalette, u32 dithering, const PolyParam &pp)
{
	return Rec { listType, sort, pass, gpuPalette, dithering, pp.tsp.full, pp.tcw.full, pp.pcw.full, pp.isp.full,
				 pp.tileclip, pp.tsp1.full, pp.tcw1.full };
}

// on a pipeline miss (render thread): the first miss for this manager + game builds every remembered pipeline
template<typename Create>
inline void preload(int kind, const void *manager, Create create)
{
	if (!on())
		return;
	KindState &k = ks(kind);
	const std::string &game = settings.content.gameId;
	if (k.manager == manager && k.game == game)
		return;
	k.manager = manager;
	k.game = game;
	k.known.clear();
	std::string path = fileFor(kind);
	if (path.empty())
		return;
	FILE *f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return;
	auto t0 = std::chrono::steady_clock::now();
	int n = 0;
	char buf[256];
	while (std::fgets(buf, sizeof(buf), f))
	{
		Rec r {};
		if (std::sscanf(buf, "%x %x %x %x %x %x %x %x %x %x %x %x", &r.listType, &r.sort, &r.pass, &r.gpuPalette,
				&r.dithering, &r.tsp, &r.tcw, &r.pcw, &r.isp, &r.tileclip, &r.tsp1, &r.tcw1) != 12)
			continue;
		if (!k.known.insert(line(r)).second)
			continue;
		PolyParam pp;
		pp.init();
		pp.tsp.full = r.tsp; pp.tcw.full = r.tcw; pp.pcw.full = r.pcw; pp.isp.full = r.isp;
		pp.tileclip = r.tileclip; pp.tsp1.full = r.tsp1; pp.tcw1.full = r.tcw1;
		pp.projMatrix = -1;		// Dreamcast polygon (not Naomi 2)
		if (create(r, pp))
			n++;
	}
	std::fclose(f);
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::fprintf(stderr, "EASYCAST_PIPES preload kind=%s game=%s built=%d known=%zu ms=%.0f\n", kind ? "oit" : "tri",
			game.c_str(), n, k.known.size(), ms);
}

// after preload: remember a pipeline this game needed (Dreamcast polygons only)
inline void record(int kind, const Rec &r, bool naomi2)
{
	if (!on() || naomi2)
		return;
	KindState &k = ks(kind);
	std::string l = line(r);
	if (!k.known.insert(l).second)
		return;
	std::string path = fileFor(kind);
	if (path.empty())
		return;
	if (FILE *f = std::fopen(path.c_str(), "a")) {
		std::fprintf(f, "%s\n", l.c_str());
		std::fclose(f);
	}
}

} // namespace easycast_pipes
