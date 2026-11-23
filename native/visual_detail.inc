// Render-only operands in the two hash-locked History Edition images. No
// on-disk EXE, game archive, simulation rank or transport state is changed.
const unsigned char Shadow4096[] = {0xb8,0x00,0x10,0x00,0x00};
const unsigned char Shadow8192[] = {0xb8,0x00,0x20,0x00,0x00};
const float NearShadowBlend = .92f; // concentrate cascade precision near camera
const std::array<Patch,4> ExtendedLodPatches=[] {
    std::array<Patch,4> result{};
    for(size_t i=0;i<result.size();++i)
        result[i]={0x51178d,{0x2c,0xe2,0x15,0x01},&visual_distance::Steps[i].lod,true};
    return result;
}();
const Patch DetailedShadowPatches[] = {
    {0x90ccf8, {0xb8,0x00,0x08,0x00,0x00}, Shadow4096, false, 5},
    // Keep the engine's resource estimate consistent with its allocations.
    {0x90dd06, {0xb8,0x00,0x08,0x00,0x00}, Shadow4096, false, 5}
};
const Patch UltraShadowPatches[] = {
    {0x90ccf8, {0xb8,0x00,0x08,0x00,0x00}, Shadow8192, false, 5},
    {0x90dd06, {0xb8,0x00,0x08,0x00,0x00}, Shadow8192, false, 5},
    {0x90ea6b, {0x4c,0xe3,0x15,0x01}, &NearShadowBlend, true}
};
using ShadowRangeLevel=std::array<Patch,4>;
const std::array<ShadowRangeLevel,4> ShadowRangeLevels=[] {
    std::array<ShadowRangeLevel,4> result{};
    for(size_t i=0;i<result.size();++i){
        const auto& step=visual_distance::Steps[i];
        result[i]={{
            {0x90ea39,{0x64,0xea,0x15,0x01},&step.shadow_span,true},
            {0x90ea4b,{0x78,0xe9,0x15,0x01},&step.shadow_ratio,true},
            {0x90eaa1,{0x00,0x00,0x48,0x46},&step.shadow_end,false,4},
            {0x90eab1,{0x00,0x00,0x48,0x47},&step.shadow_far_end,false,4}
        }};
    }
    return result;
}();

bool ShadowRangeOpcodes() {
    const auto& ShadowRangePatches=ShadowRangeLevels.front();
    const unsigned char span[] = {0xf3,0x0f,0x59,0x05};
    const unsigned char ratio[] = {0xf3,0x0f,0x10,0x05};
    const unsigned char end[] = {0xc7,0x87,0x2c,0x15,0x00,0x00};
    const unsigned char far_end[] = {0xc7,0x87,0x30,0x15,0x00,0x00};
    return !memcmp(PatchAddress(ShadowRangePatches[0])-4,span,sizeof(span))
        && !memcmp(PatchAddress(ShadowRangePatches[1])-4,ratio,sizeof(ratio))
        && !memcmp(PatchAddress(ShadowRangePatches[2])-6,end,sizeof(end))
        && !memcmp(PatchAddress(ShadowRangePatches[3])-6,far_end,sizeof(far_end));
}

bool VisualFlag(const wchar_t* name, bool& enabled) {
    wchar_t value[3] = {};
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableW(name, value, 3);
    if (!length && GetLastError() == ERROR_ENVVAR_NOT_FOUND) { enabled = false; return true; }
    if (length != 1 || (value[0] != L'0' && value[0] != L'1')) return false;
    enabled = value[0] == L'1';
    return true;
}

#include "shadow_filter.inc"

bool AppendVisualPatches(std::vector<const Patch*>& selected) {
    bool shadows=false, ultra=false;
    unsigned detail=0, range=0;
    if (!visual_distance::ReadStep(L"S7_EXTENDED_DETAIL",detail) || !VisualFlag(L"S7_DETAILED_SHADOWS",shadows)
        || !visual_distance::ReadStep(L"S7_EXTENDED_SHADOW_RANGE",range) || !VisualFlag(L"S7_ULTRA_SHADOWS",ultra)) {
        SetError(InvalidConfiguration); return false;
    }
    if ((shadows || ultra) && !PrepareShadowFilter()) { SetError(ChangedCallsite); return false; }
    if (range && !ShadowRangeOpcodes()) { SetError(ChangedCallsite); return false; }
    if (ultra) {
        const unsigned char opcode[] = {0xf3,0x0f,0x59,0x05};
        if (memcmp(PatchAddress(UltraShadowPatches[2])-4,opcode,4)) {
            SetError(ChangedCallsite); return false;
        }
    }
    if (detail) {
        const auto& ExtendedLodPatch=ExtendedLodPatches[detail-1];
        // Guard the opcode as well as the ASLR-adjusted constant operand. The
        // original setter still recomputes its resolution-dependent scale;
        // model LOD and SpeedTree both use the resulting squared distances.
        const unsigned char opcode[] = {0xf3,0x0f,0x59,0x05};
        if (memcmp(PatchAddress(ExtendedLodPatch) - sizeof(opcode), opcode, sizeof(opcode))) {
            SetError(ChangedCallsite); return false;
        }
        selected.push_back(&ExtendedLodPatch);
    }
    if (ultra) for (const auto& patch : UltraShadowPatches) selected.push_back(&patch);
    else if (shadows) for (const auto& patch : DetailedShadowPatches) selected.push_back(&patch);
    if (shadows || ultra) selected.push_back(&ShadowFilterPatch);
    if (range) for (const auto& patch : ShadowRangeLevels[range-1]) selected.push_back(&patch);
    return true;
}
