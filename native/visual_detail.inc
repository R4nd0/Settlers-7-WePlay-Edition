// Render-only operands in the two hash-locked History Edition images. No
// on-disk EXE, game archive, simulation rank or transport state is changed.
const float ExtendedLodScale = 1.0f / 200.0f; // original resolution factor: 1/300
// Preserve the original near plane, logarithmic/linear blend and five shadow
// volumes. Extend the detailed terminal from 12800 to 19200 and the far terminal
// from 51200 to 76800. Span and ratio must describe that same new terminal.
const float ExtendedShadowSpan = 19100.0f, ExtendedShadowRatio = 192.0f;
const float ExtendedShadowEnd = 19200.0f, ExtendedShadowFarEnd = 76800.0f;
const unsigned char Shadow4096[] = {0xb8,0x00,0x10,0x00,0x00};
const unsigned char Shadow8192[] = {0xb8,0x00,0x20,0x00,0x00};
const float NearShadowBlend = .92f; // concentrate cascade precision near camera
const Patch ExtendedLodPatch = {0x51178d, {0x2c,0xe2,0x15,0x01}, &ExtendedLodScale, true};
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
const Patch ShadowRangePatches[] = {
    {0x90ea39, {0x64,0xea,0x15,0x01}, &ExtendedShadowSpan, true},
    {0x90ea4b, {0x78,0xe9,0x15,0x01}, &ExtendedShadowRatio, true},
    {0x90eaa1, {0x00,0x00,0x48,0x46}, &ExtendedShadowEnd, false, 4},
    {0x90eab1, {0x00,0x00,0x48,0x47}, &ExtendedShadowFarEnd, false, 4}
};

bool ShadowRangeOpcodes() {
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

bool AppendVisualPatches(std::vector<const Patch*>& selected) {
    bool detail = false, shadows = false, range = false, ultra = false;
    if (!VisualFlag(L"S7_EXTENDED_DETAIL", detail) || !VisualFlag(L"S7_DETAILED_SHADOWS", shadows)
        || !VisualFlag(L"S7_EXTENDED_SHADOW_RANGE",range) || !VisualFlag(L"S7_ULTRA_SHADOWS",ultra)) {
        SetError(InvalidConfiguration); return false;
    }
    if (range && !ShadowRangeOpcodes()) { SetError(ChangedCallsite); return false; }
    if (ultra) {
        const unsigned char opcode[] = {0xf3,0x0f,0x59,0x05};
        if (memcmp(PatchAddress(UltraShadowPatches[2])-4,opcode,4)) {
            SetError(ChangedCallsite); return false;
        }
    }
    if (detail) {
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
    if (range) for (const auto& patch : ShadowRangePatches) selected.push_back(&patch);
    return true;
}
