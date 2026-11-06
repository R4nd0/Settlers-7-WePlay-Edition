// Render-only operands in the two hash-locked History Edition images. No
// on-disk EXE, game archive, simulation rank or transport state is changed.
const float ExtendedLodScale = 1.0f / 200.0f; // original resolution factor: 1/300
const unsigned char Shadow4096[] = {0xb8,0x00,0x10,0x00,0x00};
const Patch ExtendedLodPatch = {0x51178d, {0x2c,0xe2,0x15,0x01}, &ExtendedLodScale, true};
const Patch DetailedShadowPatches[] = {
    {0x90ccf8, {0xb8,0x00,0x08,0x00,0x00}, Shadow4096, false, 5},
    // Keep the engine's resource estimate consistent with its allocations.
    {0x90dd06, {0xb8,0x00,0x08,0x00,0x00}, Shadow4096, false, 5}
};

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
    bool detail = false, shadows = false;
    if (!VisualFlag(L"S7_EXTENDED_DETAIL", detail) || !VisualFlag(L"S7_DETAILED_SHADOWS", shadows)) {
        SetError(InvalidConfiguration); return false;
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
    if (shadows) for (const auto& patch : DetailedShadowPatches) selected.push_back(&patch);
    return true;
}
