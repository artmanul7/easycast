// EasyCast (Claude) 14.11: per-pixel (OIT) renderer lab for Shenmue on Mali-G52.
// Every translucent fragment takes a slot of the fragment buffer with atomicAdd on ONE global counter.
//  EASYCAST_OIT_AGG=1   (OFF by default: flickering textures in the Shenmue intro, 14.11) one atomicAdd per subgroup (ballot + exclusive bit count), same slots per fragment set,
//                       same picture (slot numbers do not change the order: lists are linked per pixel and
//                       sorted by depth/poly). Only if the GPU has subgroup ballot + arithmetic in fragment shaders.
//  EASYCAST_OIT_X=1     measuring only: translucent fragments are not stored (no atomics, no fragment writes)
//  EASYCAST_OIT_X=2     measuring only: the final pass does not sort/blend (shows opaque only)
//  EASYCAST_OIT_STATS=1 "EASYCAST_OIT frames= frags_avg= frags_max=" every 5 s (fragments per screen frame)
#pragma once
#include "../vulkan_context.h"
#include "../buffer.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace easycast_oit {

inline int envInt(const char *n, int def)
{
	const char *e = std::getenv(n);
	return e != nullptr && *e ? std::atoi(e) : def;
}
inline int mode() { static const int v = envInt("EASYCAST_OIT_X", 0); return v; }
// EASYCAST_OIT_NC=1: fragment buffer without "coherent" (append pass writeonly, final pass readonly; the subpass
//   dependency already makes the writes visible) -> reads may use the GPU caches
inline bool nc() { static const bool v = envInt("EASYCAST_OIT_NC", 0) == 1; return v; }
// EASYCAST_OIT_LOCAL=1: final pass reads every fragment once and sorts local copies of depth / poly index
inline bool local() { static const bool v = envInt("EASYCAST_OIT_LOCAL", 0) == 1; return v; }
// EASYCAST_OIT_SEL=1: final pass without any local array (Mali keeps them in memory): the fragments are taken in
//   the same order by repeated selection over the pixel's list (depth, poly index, list position)
inline bool sel() { static const bool v = envInt("EASYCAST_OIT_SEL", 0) == 1; return v; }
// EASYCAST_OIT_Z1=1 (step 5): a screen frame with ONE render pass and WITHOUT opaque modifier volumes (shadows) draws
//   opaque and punch-through polygons once, in subpass 0, with colour and depth (Pass::ColorPre), instead of a depth
//   pre-pass in subpass 0 + colour pass in subpass 1. The translucent manual depth test keeps reading subpass 0's
//   depth (= the same opaque depth). Stencil is cleared in the first pass, so no pixel is shadowed (0x81) without
//   a volume. (Step 4 skipped the pre-pass without moving the colour pass: wrong, translucent test saw no depth.)
inline bool z1() { static const bool v = envInt("EASYCAST_OIT_Z1", 1) == 1; return v; }	// 14.11 ON by default (=0 off)

// per-frame scene counters (CPU side, EASYCAST_OIT_STATS=1)
struct Scene { uint64_t frames = 0, passes = 0, mvFrames = 0, skipped = 0, op = 0, pt = 0, tr = 0; };
inline Scene &scene() { static Scene s; return s; }

// subgroup aggregation of the fragment counter: requested and supported?
inline bool agg()
{
	static const bool v = [] {
		if (envInt("EASYCAST_OIT_AGG", 0) != 1)		// OFF: 14.11 the user saw many textures flicker with it (Shenmue intro)
			return false;
		VulkanContext *ctx = VulkanContext::Instance();
		vk::PhysicalDevice phys = ctx->GetPhysicalDevice();
		if (phys.getProperties().apiVersion < VK_API_VERSION_1_1) {
			std::fprintf(stderr, "EASYCAST_OIT agg off: Vulkan 1.0 device\n");
			return false;
		}
		auto chain = phys.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceSubgroupProperties>();
		const auto &sg = chain.get<vk::PhysicalDeviceSubgroupProperties>();
		const bool frag = (bool)(sg.supportedStages & vk::ShaderStageFlagBits::eFragment);
		const bool ballot = (bool)(sg.supportedOperations & vk::SubgroupFeatureFlagBits::eBallot);
		const bool arith = (bool)(sg.supportedOperations & vk::SubgroupFeatureFlagBits::eArithmetic);
		std::fprintf(stderr, "EASYCAST_OIT subgroup size=%u stages=0x%x ops=0x%x -> agg %s\n", sg.subgroupSize,
				(unsigned)(VkShaderStageFlags)sg.supportedStages, (unsigned)(VkSubgroupFeatureFlags)sg.supportedOperations,
				frag && ballot && arith ? "ON" : "off (unsupported)");
		return frag && ballot && arith;
	}();
	return v;
}

// fragment counter read-back (screen frames): copy the counter after the frame, read it 8 frames later
struct Stats {
	static constexpr unsigned SLOTS = 8;
	std::unique_ptr<BufferData> buf;
	bool written[SLOTS] {};
	unsigned frame = 0;
	uint64_t sum = 0, max = 0, n = 0;
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	~Stats() { (void)buf.release(); }	// at process exit the device may be gone: never free here
};
inline Stats &st() { static Stats s; return s; }

// before the Vulkan device goes away (OITScreenDrawer::Term): a static buffer freed at exit crashed (status 139)
inline void term()
{
	Stats &s = st();
	s.buf.reset();
	for (auto &w : s.written) w = false;
}

inline void frameEnd(vk::CommandBuffer cmd, vk::Buffer counter, bool screen)
{
	static const bool on = envInt("EASYCAST_OIT_STATS", 0) == 1;
	if (!on || !screen)
		return;
	Stats &s = st();
	if (!s.buf)
		s.buf = std::make_unique<BufferData>(Stats::SLOTS * 4, vk::BufferUsageFlagBits::eTransferDst);
	const unsigned slot = s.frame % Stats::SLOTS;
	if (s.written[slot]) {
		uint32_t v = 0;
		s.buf->download(4, &v, slot * 4);
		s.sum += v;
		s.n++;
		if (v > s.max) s.max = v;
	}
	vk::MemoryBarrier mb(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, mb, nullptr, nullptr);
	cmd.copyBuffer(counter, *s.buf->buffer, vk::BufferCopy(0, slot * 4, 4));
	s.written[slot] = true;
	s.frame++;
	auto now = std::chrono::steady_clock::now();
	if (now - s.t0 >= std::chrono::seconds(5)) {
		Scene &c = scene();
		std::fprintf(stderr, "EASYCAST_OIT frames=%llu frags_avg=%llu frags_max=%llu agg=%d x=%d z1=%d | passes=%llu "
				"mv_frames=%llu prepass_skipped=%llu op_avg=%llu pt_avg=%llu tr_avg=%llu\n", (unsigned long long)s.n,
				(unsigned long long)(s.n ? s.sum / s.n : 0), (unsigned long long)s.max, (int)agg(), mode(), (int)z1(),
				(unsigned long long)c.passes, (unsigned long long)c.mvFrames, (unsigned long long)c.skipped,
				(unsigned long long)(c.frames ? c.op / c.frames : 0), (unsigned long long)(c.frames ? c.pt / c.frames : 0),
				(unsigned long long)(c.frames ? c.tr / c.frames : 0));
		c = Scene();
		s.sum = s.max = s.n = 0;
		s.t0 = now;
	}
}

} // namespace easycast_oit
