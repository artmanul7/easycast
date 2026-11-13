// EasyCast 0.2.2 (Claude): share per-polygon descriptor sets between polygons that use the same texture + sampler.
// Flycast allocated and wrote one descriptor set per textured polygon per frame (hundreds..thousands); for
// non-Naomi2 polygons the set only holds the texture(s), so identical ones are reused within the frame: fewer
// vkAllocateDescriptorSets/vkUpdateDescriptorSets calls in the Mali driver. EASYCAST_SHARE_DESC=0 -> old way.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>

struct EasycastTexKey {
	uint64_t s0, v0, s1, v1;
	bool operator==(const EasycastTexKey &o) const { return s0 == o.s0 && v0 == o.v0 && s1 == o.s1 && v1 == o.v1; }
};
struct EasycastTexKeyHash {
	size_t operator()(const EasycastTexKey &k) const
	{
		uint64_t h = k.s0 * 0x9E3779B97F4A7C15ull;
		h ^= k.v0 + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
		h ^= k.s1 + 0x8CB92BA72F3D8DD7ull + (h << 6) + (h >> 2);
		h ^= k.v1 + 0xD6E8FEB86659FD93ull + (h << 6) + (h >> 2);
		return (size_t)h;
	}
};
inline bool easycastShareDescSets()
{
	static const bool on = [] {
		const char *e = std::getenv("EASYCAST_SHARE_DESC");
		return !(e != nullptr && std::strcmp(e, "0") == 0);
	}();
	return on;
}
