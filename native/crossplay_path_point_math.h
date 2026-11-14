// Original History float steps for native path interpolation. No game access.
#pragma once
#include <xmmintrin.h>
namespace HistoryPathPointMath {
inline void Point(float ratio,float cx,float cy,float px,float py,bool plus,float* result) {
 const __m128 epsilon=_mm_set_ss(0.00999999977648258209228515625f);
 const __m128 mix=plus?_mm_add_ss(_mm_set_ss(ratio),epsilon):_mm_sub_ss(_mm_set_ss(ratio),epsilon);
 const __m128 inverse=_mm_sub_ss(_mm_set_ss(1.0f),mix);
 result[0]=_mm_cvtss_f32(_mm_add_ss(_mm_mul_ss(mix,_mm_set_ss(cx)),_mm_mul_ss(inverse,_mm_set_ss(px))));
 result[1]=_mm_cvtss_f32(_mm_add_ss(_mm_mul_ss(mix,_mm_set_ss(cy)),_mm_mul_ss(inverse,_mm_set_ss(py))));
}
}
