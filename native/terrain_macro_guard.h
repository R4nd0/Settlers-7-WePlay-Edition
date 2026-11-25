// SPDX-License-Identifier: MIT
#pragma once
#include <d3d9.h>

namespace TerrainMacroGuard {
// Source-proven meaningful components only. Unused region/detail components
// are never interpreted, normalized or written. Equality rejects NaN/Inf.
inline bool Constants(const float region[4],const float detail[4]) noexcept {
    return region&&detail&&region[2]==3200.0f&&region[3]==3200.0f
        &&detail[0]==512.0f&&detail[1]==9.0f&&detail[2]==8.0f;
}
inline bool Texture(unsigned sampler,D3DRESOURCETYPE type,UINT levels,const D3DSURFACE_DESC& desc) noexcept {
    if(type!=D3DRTYPE_TEXTURE)return false;
    if(sampler==4||sampler==5)return levels==10&&desc.Width==512&&desc.Height==512&&desc.Format==D3DFMT_DXT5;
    return sampler==6&&levels==9&&desc.Width==256&&desc.Height==256&&desc.Format==D3DFMT_L8;
}
}
