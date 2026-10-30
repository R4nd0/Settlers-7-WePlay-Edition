// Tests the real Steam patch writer against owned synthetic memory only.
#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool result) { ++checks; if (!result) ++failures; };
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1106000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    steam_history = true;
    auto accepts = [](const char* hex) {
        unsigned char hash[32]{};
        for (size_t i = 0; i < 32; ++i) {
            unsigned int value = 0;
            if (std::sscanf(hex + i * 2, "%2x", &value) != 1) return false;
            hash[i] = static_cast<unsigned char>(value);
        }
        return AcceptExecutableDigest(hash);
    };
    constexpr char SteamOriginal[] = "8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410";
    constexpr char UbisoftOriginal[] = "4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f";
    constexpr char SteamLabC[] = "cf7f5f25665d8e98ff3b17a70e658be4458f67b29c42455c83715a4bb68c7d2d";
    constexpr char SteamLabD[] = "ce9f64801cd6e837df287e9a5c180d653f4e14814cd2806d2cb8b898106fc810";
    check(!accepts(UbisoftOriginal));
#ifdef S7_LOCAL_PAIR_LAB
    check(!accepts(SteamOriginal));
    check(accepts(SteamLabC) && local_pair_bind.load() == 0x7f000002u);
    check(accepts(SteamLabD) && local_pair_bind.load() == 0x7f000003u);
#else
    check(accepts(SteamOriginal));
    check(!accepts(SteamLabC)); check(!accepts(SteamLabD));
#endif
    check(!accepts("b45794aef1ddef76fea13501fa7630023f0c11653c2f644a06c0ae86cb0812b4"));
    for (const auto& address : SteamHistory::Addresses) {
        check(NativeVA(address.ubisoft) == image_base + address.steam - PreferredBase);
        check(address.steam >= PreferredBase && address.steam < PreferredBase + 0x1106000);
    }
    std::vector<const Patch*> selected;
    for (const auto& patch : Patches) selected.push_back(&patch);
    selected.push_back(&DevelopmentPatch);
    for (const auto& patch : ConnectPatches) selected.push_back(&patch);
    auto reset = [&]() {
        for (const auto* patch : selected) {
            if (patch->pointer_slot) {
                const uint32_t pointer = OriginalPointer(*patch);
                memcpy(PatchAddress(*patch), &pointer, sizeof(pointer));
            } else memcpy(PatchAddress(*patch), OriginalBytes(*patch), patch->raw_length ? patch->raw_length : 5);
        }
        memcpy(Native<void*>(0x770906), RewardUnlockedEpilogue, sizeof(RewardUnlockedEpilogue));
    };
    local_connect_probe = true; developer_mode = true; lan_game = false;
    reset(); check(Install());
    for (const auto* patch : selected) check(WritePatch(*patch, true));
    for (const auto* patch : selected) {
        check(patch->pointer_slot
            ? *reinterpret_cast<uint32_t*>(PatchAddress(*patch)) == OriginalPointer(*patch)
            : memcmp(PatchAddress(*patch), OriginalBytes(*patch), patch->raw_length ? patch->raw_length : 5) == 0);
    }
    // Corruption of the final guarded site must reject before any write.
    reset(); auto* final_site = PatchAddress(*selected.back()); *final_site ^= 1;
    check(!Install() && error_code == ChangedCallsite);
    for (size_t i = 0; i + 1 < selected.size(); ++i) {
        const auto& patch = *selected[i];
        check(patch.pointer_slot
            ? *reinterpret_cast<uint32_t*>(PatchAddress(patch)) == OriginalPointer(patch)
            : memcmp(PatchAddress(patch), OriginalBytes(patch), patch.raw_length ? patch.raw_length : 5) == 0);
    }
    steam_history = false;
    for (const auto& address : SteamHistory::Addresses)
        check(NativeVA(address.ubisoft) == image_base + address.ubisoft - PreferredBase);
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("%s %d Steam profile checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
