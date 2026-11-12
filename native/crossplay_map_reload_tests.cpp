#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "crossplay_history_map_reload.h"

extern "C" __declspec(dllexport) uint32_t __cdecl WePlayMapReloadMethodsOracle(uint32_t edition) {
    return CrossplayNative::HistoryMapReloadDetail::Guard(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
int main() {
    if(WePlayMapReloadMethodsOracle(0) || WePlayMapReloadMethodsOracle(1)
        || WePlayMapReloadMethodsOracle(4) || WePlayMapReloadMethodsOracle(UINT32_MAX))return 1;
    return 0;
}
