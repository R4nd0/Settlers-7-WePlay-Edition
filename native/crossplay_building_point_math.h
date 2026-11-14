#pragma once
#include <emmintrin.h>
#include <cstdint>
#include <cstring>
namespace BuildingPointMath {
struct Point {float x,y;};
struct Rotation {float cosine,sine;};
inline Rotation RotateDegrees(float degrees) {
    const __m128 radians=_mm_div_ss(_mm_mul_ss(_mm_set_ss(degrees),_mm_set_ss(3.1415927410125732421875f)),_mm_set_ss(180.0f));
    float angle=_mm_cvtss_f32(radians);Rotation result{};
    __asm {fld angle
           fcos
           fstp result.cosine
           fld angle
           fsin
           fstp result.sine}
    return result;
}
inline Point Transform(Point local,Point position,Rotation rotation) {
    const __m128 x=_mm_set_ss(local.x),y=_mm_set_ss(local.y),c=_mm_set_ss(rotation.cosine),s=_mm_set_ss(rotation.sine);
    const __m128 minus_s=_mm_xor_ps(s,_mm_castsi128_ps(_mm_set_epi32(0,0,0,static_cast<int>(0x80000000u))));
    const __m128 rx=_mm_add_ss(_mm_mul_ss(x,c),_mm_mul_ss(y,s));
    const __m128 ry=_mm_add_ss(_mm_mul_ss(x,minus_s),_mm_mul_ss(y,c));
    return {_mm_cvtss_f32(_mm_add_ss(_mm_set_ss(position.x),rx)),_mm_cvtss_f32(_mm_add_ss(_mm_set_ss(position.y),ry))};
}
inline bool ZeroBits(Point p) {uint32_t bits[2]{};memcpy(bits,&p,8);return bits[0]==0 && bits[1]==0;}
}
