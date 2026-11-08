// Exercise the production operand guards, selection and rollback on synthetic
// executable memory. Native engine calls and game assets are never executed.
#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}

void PrepareFilterContext() {
    auto* at = PatchAddress(ShadowFilterPatch);
    const unsigned char index[] = {0xf3,0x0f,0x10,0x84,0xb4,0xc8,0,0,0};
    const unsigned char before[] = {0x83,0xc4,0x0c};
    const unsigned char after[] = {
        0x50,0xff,0xb3,0xac,0x19,0,0,0x8b,0x83,0xb0,0x19,0,0,0xff,0xd0
    };
    memcpy(at-0x52, index, sizeof(index));
    memcpy(at-3, before, sizeof(before));
    memcpy(at+5, after, sizeof(after));
    memcpy(at, OriginalBytes(ShadowFilterPatch), 5);
}

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) ++failures; };
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1106000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL", nullptr);
    SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS", nullptr);
    SetEnvironmentVariableW(L"S7_EXTENDED_SHADOW_RANGE", nullptr);
    SetEnvironmentVariableW(L"S7_ULTRA_SHADOWS", nullptr);
    std::vector<const Patch*> selected;
    check(AppendVisualPatches(selected) && selected.empty());
    for (const wchar_t* invalid : {L"", L"true", L"2", L"01", L" 1"}) {
        SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL", invalid);
        check(!AppendVisualPatches(selected) && selected.empty());
    }
    SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL", L"1");
    SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS", L"1");
    for (bool steam : {false, true}) {
        steam_history = steam;
        PrepareFilterContext();
        auto* operand = PatchAddress(ExtendedLodPatch);
        const unsigned char opcode[] = {0xf3,0x0f,0x59,0x05};
        memcpy(operand - 4, opcode, 4);
        uint32_t original = OriginalPointer(ExtendedLodPatch);
        memcpy(operand, &original, 4);
        for (const auto& patch : DetailedShadowPatches)
            memcpy(PatchAddress(patch), OriginalBytes(patch), 5);
        selected.clear();
        check(AppendVisualPatches(selected) && selected.size() == 4);
        DWORD previous = 0;
        check(VirtualProtect(memory, 0x1106000, PAGE_EXECUTE_READ, &previous) != FALSE);
        for (const auto* patch : selected) {
            check(WritePatch(*patch, false));
            MEMORY_BASIC_INFORMATION info{};
            check(VirtualQuery(PatchAddress(*patch), &info, sizeof(info)) == sizeof(info)
                && info.Protect == PAGE_EXECUTE_READ);
        }
        check(*reinterpret_cast<uint32_t*>(operand) == reinterpret_cast<uintptr_t>(&ExtendedLodScale));
        for (const auto& patch : DetailedShadowPatches)
            check(!memcmp(PatchAddress(patch), Shadow4096, 5));
        for (const auto* patch : selected) check(WritePatch(*patch, true));
        check(*reinterpret_cast<uint32_t*>(operand) == original);
        for (const auto& patch : DetailedShadowPatches)
            check(!memcmp(PatchAddress(patch), OriginalBytes(patch), 5));
        check(VirtualProtect(memory, 0x1106000, PAGE_READWRITE, &previous) != FALSE);
        operand[-1] = 0x04;
        selected.clear();
        check(!AppendVisualPatches(selected) && selected.empty());
    }
    SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL", L"0");
    SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS", L"0");
    selected.clear(); check(AppendVisualPatches(selected) && selected.empty());
    SetEnvironmentVariableW(L"S7_EXTENDED_SHADOW_RANGE", L"1");
    for (bool steam : {false,true}) {
        steam_history=steam;
        const unsigned char span[]={0xf3,0x0f,0x59,0x05}, ratio[]={0xf3,0x0f,0x10,0x05};
        const unsigned char end[]={0xc7,0x87,0x2c,0x15,0x00,0x00}, far_end[]={0xc7,0x87,0x30,0x15,0x00,0x00};
        memcpy(PatchAddress(ShadowRangePatches[0])-4,span,4);
        memcpy(PatchAddress(ShadowRangePatches[1])-4,ratio,4);
        memcpy(PatchAddress(ShadowRangePatches[2])-6,end,6);
        memcpy(PatchAddress(ShadowRangePatches[3])-6,far_end,6);
        for (const auto& patch:ShadowRangePatches) {
            if (patch.pointer_slot) {uint32_t value=OriginalPointer(patch);memcpy(PatchAddress(patch),&value,4);}
            else memcpy(PatchAddress(patch),OriginalBytes(patch),4);
        }
        selected.clear();check(AppendVisualPatches(selected)&&selected.size()==4);
        for (const auto* patch:selected) {
            check(WritePatch(*patch,false));
            if (patch->pointer_slot) check(*reinterpret_cast<uint32_t*>(PatchAddress(*patch))==reinterpret_cast<uintptr_t>(patch->replacement));
            else check(!memcmp(PatchAddress(*patch),patch->replacement,4));
            check(WritePatch(*patch,true));
            if (patch->pointer_slot) check(*reinterpret_cast<uint32_t*>(PatchAddress(*patch))==OriginalPointer(*patch));
            else check(!memcmp(PatchAddress(*patch),OriginalBytes(*patch),4));
        }
        PatchAddress(ShadowRangePatches[2])[-3]=0x14;
        selected.clear();check(!AppendVisualPatches(selected)&&selected.empty());
    }
    for (const wchar_t* invalid:{L"",L"true",L"2",L"01"}) {
        SetEnvironmentVariableW(L"S7_EXTENDED_SHADOW_RANGE",invalid);
        selected.clear();check(!AppendVisualPatches(selected)&&selected.empty());
    }
    SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL", nullptr);
    SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS", nullptr);
    SetEnvironmentVariableW(L"S7_EXTENDED_SHADOW_RANGE", nullptr);
    SetEnvironmentVariableW(L"S7_ULTRA_SHADOWS", L"1");
    for (bool steam : {false,true}) {
        steam_history=steam;
        const unsigned char opcode[]={0xf3,0x0f,0x59,0x05};
        PrepareFilterContext();
        memcpy(PatchAddress(UltraShadowPatches[2])-4,opcode,4);
        for (const auto& patch:UltraShadowPatches) {
            if (patch.pointer_slot) {uint32_t value=OriginalPointer(patch);memcpy(PatchAddress(patch),&value,4);}
            else memcpy(PatchAddress(patch),OriginalBytes(patch),patch.raw_length);
        }
        // Ultra independently selects 8192 and the near-camera split blend;
        // the ordinary detailed flag must not add competing writes.
        for (bool detailed:{false,true}) {
            SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS",detailed?L"1":L"0");
            selected.clear();check(AppendVisualPatches(selected)&&selected.size()==4);
            for (const auto* patch:selected) {
                check(WritePatch(*patch,false));
                if (patch->pointer_slot) check(*reinterpret_cast<uint32_t*>(PatchAddress(*patch))==reinterpret_cast<uintptr_t>(&NearShadowBlend));
                else if (patch == &ShadowFilterPatch) check(PatchAddress(*patch)[0] == 0xe8);
                else check(!memcmp(PatchAddress(*patch),Shadow8192,5));
                check(WritePatch(*patch,true));
            }
        }
        PatchAddress(UltraShadowPatches[2])[-1]=0;
        selected.clear();check(!AppendVisualPatches(selected)&&selected.empty());
    }
    for (const wchar_t* invalid:{L"",L"true",L"2",L"01"}) {
        SetEnvironmentVariableW(L"S7_ULTRA_SHADOWS",invalid);
        selected.clear();check(!AppendVisualPatches(selected)&&selected.empty());
    }
    SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS",nullptr);
    SetEnvironmentVariableW(L"S7_ULTRA_SHADOWS",nullptr);
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("%d visual-detail checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
