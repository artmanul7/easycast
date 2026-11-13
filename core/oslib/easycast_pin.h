// EasyCast 0.1.7 (Claude): pin Flycast threads to CPU cores by thread name.
// EASYCAST_PIN unset -> default map; EASYCAST_PIN=0 -> off; otherwise "Name=cpus;Name=cpus" (cpus: "3", "0-2", "1,2").
// Default: emulation (SH4) alone on core 3, render/main thread on 0-2 (threads it creates later inherit 0-2,
// so Mali/SDL/present threads stay off core 3), sound (AICA/ARM7) on core 1.
#pragma once
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace easycast_pin {

inline const char *spec()
{
	static const char *s = [] {
		const char *e = std::getenv("EASYCAST_PIN");
		if (e == nullptr || *e == '\0')
			e = "Flycast-emu=3;Flycast-rend=0-2;Flycast-aica=1";
		return e;
	}();
	return s;
}

inline bool parseCpus(const char *p, const char *end, cpu_set_t &set)
{
	CPU_ZERO(&set);
	bool any = false;
	while (p < end) {
		char *q;
		long a = std::strtol(p, &q, 10);
		if (q == p) return false;
		long b = a;
		p = q;
		if (p < end && *p == '-') {
			b = std::strtol(p + 1, &q, 10);
			if (q == p + 1) return false;
			p = q;
		}
		for (long c = a; c <= b && c < CPU_SETSIZE; c++) {
			CPU_SET((int)c, &set);
			any = true;
		}
		if (p < end && *p == ',') p++;
		else if (p < end) return false;
	}
	return any;
}

// called right after the current thread got its name
inline void apply(const char *name)
{
	const char *s = spec();
	if (std::strcmp(s, "0") == 0)
		return;
	const size_t nlen = std::strlen(name);
	const char *p = s;
	while (*p) {
		const char *semi = std::strchr(p, ';');
		const char *end = semi ? semi : p + std::strlen(p);
		const char *eq = (const char *)std::memchr(p, '=', end - p);
		if (eq && (size_t)(eq - p) == nlen && std::strncmp(p, name, nlen) == 0) {
			cpu_set_t set;
			if (parseCpus(eq + 1, end, set)) {
				int r = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
				std::fprintf(stderr, "EASYCAST_PIN %s cpus=%.*s %s\n", name, (int)(end - eq - 1), eq + 1, r == 0 ? "ok" : "failed");
			}
			return;
		}
		if (!semi) break;
		p = semi + 1;
	}
}

} // namespace easycast_pin
#endif
