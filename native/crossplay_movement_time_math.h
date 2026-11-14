#pragma once
// Exact single-precision History movement-time arithmetic. The native caller
// validates finite distance, positive speed and bounded integer results.
// This pure helper neither installs hooks nor changes any world state.
#include <emmintrin.h>
namespace MovementTimeMath {
inline float Divide(float distance,float multiplier) {
 return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(distance),_mm_set_ss(multiplier)));
}
inline int Milliseconds(float distance,float multiplier) {
 return _mm_cvtt_ss2si(_mm_div_ss(_mm_set_ss(distance),_mm_set_ss(multiplier)));
}
}
