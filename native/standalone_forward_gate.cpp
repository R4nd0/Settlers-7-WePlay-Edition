#include "standalone_gate.h"
// Every Miles export is an exact PE forwarder to the user's retained original.
// No wrapper guesses an audio ABI. Direct game starts fail before game code.
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID) {
    return reason != DLL_PROCESS_ATTACH || StandaloneLaunchGate();
}
