#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ai_catalog_unlock.h"
#include <cstdio>
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayAICatalogMethodsOracle(uint32_t edition) {
    return WePlayAICatalog::Methods(edition)?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayAICatalogInstallOracle(uint32_t edition) {
    return WePlayAICatalog::Install(edition)?1u:0u;
}
int main(){std::puts("Offline original-image guard and patch fixture only.");return 2;}
