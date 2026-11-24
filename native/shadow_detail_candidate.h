// SPDX-License-Identifier: MIT
// Staged policy only: no device calls, allocation, shader/source copying or enablement.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace ShadowDetailCandidate {
enum class Mode : uint32_t { Off=0, SoftHighResolutionEdges=1 };
enum class Decision : uint32_t { Disabled=0, Apply=1, UnsupportedMode=2,
    UnmatchedProgram=3, UnmatchedSampler=4, PreserveExistingFarCorrection=5,
    UnknownFootprint=6 };
constexpr char VertexSha[]="7744611870305c0b6b9a330010cda3599c34080774b7f11bef367fdcd21eb698";
constexpr char PixelSha[]="6be44bdf12ff3d9b94cadc9fed076c55e26207d4a4233120e5c073a76dcd38ea";
constexpr uint32_t VertexBytes=856,PixelBytes=2216,PixelConstant=57,ShadowSampler=1;

struct Input {
    Mode mode=Mode::Off;
    std::array<unsigned char,32> vertex_sha{},pixel_sha{};
    uint32_t vertex_bytes=0,pixel_bytes=0;
    bool sampler_is_2d=false;
    uint32_t sampler_width=0,sampler_height=0;
    std::array<float,4> original_inverse{};
};
struct Plan {
    Decision decision=Decision::Disabled;
    std::array<float,4> inverse{};
    uint32_t pixel_constant=PixelConstant;
    // The candidate changes only c57.xy by an exact 1.25 factor. It does not
    // change c33 kernel radii/noise frequency, taps, maps, bias or cascade splits.
};

inline bool Hash(const std::array<unsigned char,32>& value,const char* expected) {
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<32;++i)
        if(value[i]!=static_cast<unsigned char>((digit(expected[2*i])<<4)|digit(expected[2*i+1])))return false;
    return true;
}

inline Plan Create(const Input& input) {
    Plan result;std::memcpy(result.inverse.data(),input.original_inverse.data(),sizeof(result.inverse));
    if(input.mode==Mode::Off)return result;
    if(input.mode!=Mode::SoftHighResolutionEdges){result.decision=Decision::UnsupportedMode;return result;}
    if(input.vertex_bytes!=VertexBytes || input.pixel_bytes!=PixelBytes
        || !Hash(input.vertex_sha,VertexSha) || !Hash(input.pixel_sha,PixelSha)) {
        result.decision=Decision::UnmatchedProgram;return result;
    }
    if(!input.sampler_is_2d || input.sampler_width!=input.sampler_height
        || (input.sampler_width!=4096 && input.sampler_width!=8192)) {
        result.decision=Decision::UnmatchedSampler;return result;
    }
    std::array<uint32_t,4> words{};
    std::memcpy(words.data(),input.original_inverse.data(),sizeof(words));
    if(words[0]==0x3a000000u && words[1]==0x3a000000u) { // 1/2048
        result.decision=Decision::PreserveExistingFarCorrection;return result;
    }
    const uint32_t native=input.sampler_width==4096?0x39800000u:0x39000000u;
    if(words[0]!=native || words[1]!=native){result.decision=Decision::UnknownFootprint;return result;}
    // Bit construction is exact and preserves the calling thread's FP state,
    // including unused z/w bit patterns. 1.25*2^-n adds a .25 mantissa.
    words[0]=words[1]=native+0x00200000u;
    std::memcpy(result.inverse.data(),words.data(),sizeof(words));
    result.decision=Decision::Apply;return result;
}
}
