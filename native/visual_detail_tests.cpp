// Exercise the production operand guards, selection and rollback on synthetic
// executable memory. Native engine calls and game assets are never executed.
#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) ++failures; };
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1106000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL", nullptr);
    SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS", nullptr);
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
        auto* operand = PatchAddress(ExtendedLodPatch);
        const unsigned char opcode[] = {0xf3,0x0f,0x59,0x05};
        memcpy(operand - 4, opcode, 4);
        uint32_t original = OriginalPointer(ExtendedLodPatch);
        memcpy(operand, &original, 4);
        for (const auto& patch : DetailedShadowPatches)
            memcpy(PatchAddress(patch), OriginalBytes(patch), 5);
        selected.clear();
        check(AppendVisualPatches(selected) && selected.size() == 3);
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
    SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL", nullptr);
    SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS", nullptr);
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("%d visual-detail checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
