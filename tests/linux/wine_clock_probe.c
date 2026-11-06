#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>

int main(void) {
    printf("{\"pointer_bits\":%u,\"tick_ms\":%llu}\n",
           (unsigned)(sizeof(void*) * 8), (unsigned long long)GetTickCount64());
    return 0;
}
