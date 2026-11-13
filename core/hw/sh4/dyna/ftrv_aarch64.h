#pragma once
#include <arm_neon.h>

// Parallel output lanes, retaining double intermediates and the original
// left-to-right sum. Read every input before writing: fd may alias fn or fm.
static inline void ftrvAarch64(float *fd, const float *fn, const float *fm)
{
	const float64x2_t row0lo = vcvt_f64_f32(vld1_f32(fm));
	const float64x2_t row0hi = vcvt_f64_f32(vld1_f32(fm + 2));
	float64x2_t lo = vmulq_n_f64(row0lo, (double)fn[0]);
	float64x2_t hi = vmulq_n_f64(row0hi, (double)fn[0]);
	for (int i = 1; i < 4; ++i)
	{
		const float64x2_t rlo = vcvt_f64_f32(vld1_f32(fm + 4 * i));
		const float64x2_t rhi = vcvt_f64_f32(vld1_f32(fm + 4 * i + 2));
		lo = vaddq_f64(lo, vmulq_n_f64(rlo, (double)fn[i]));
		hi = vaddq_f64(hi, vmulq_n_f64(rhi, (double)fn[i]));
	}
	float result[4];
	vst1_f32(result, vcvt_f32_f64(lo));
	vst1_f32(result + 2, vcvt_f32_f64(hi));
	for (int i = 0; i < 4; ++i)
		fd[i] = fixNaN(result[i]);
}
