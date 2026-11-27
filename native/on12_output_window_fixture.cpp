#include "on12_output_window.h"
#include <cstdio>
#include <climits>
namespace {
unsigned checks = 0; POINT mouse{}; SIZE logical{};
bool Check(bool okay, unsigned line) { ++checks; if (!okay) std::fprintf(stderr, "window check failed at %u\n", line); return okay; }
#define REQUIRE(x) do { if (!Check(!!(x), __LINE__)) return 20; } while (false)
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_SIZE) logical = {LOWORD(l), HIWORD(l)};
    if (message == WM_MOUSEMOVE) mouse = {static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
    return DefWindowProcW(window, message, w, l);
}
bool Busy() { return false; }
}
int main() {
    using On12Graphics::ScalePoint;
    POINT p{960, 540}; REQUIRE(ScalePoint(p, 1920, 1080, 1280, 720) && p.x == 640 && p.y == 360);
    p = {-3, -3}; REQUIRE(ScalePoint(p, 1920, 1080, 1280, 720) && p.x == -2 && p.y == -2);
    p = {LONG_MAX, LONG_MIN}; const POINT before = p;
    REQUIRE(!ScalePoint(p, 1, 1, 16384, 16384) && p.x == before.x && p.y == before.y);
    REQUIRE(!ScalePoint(p, 0, 1, 1, 1));
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = Procedure; wc.lpszClassName = L"WePlayOn12OutputFixture";
    REQUIRE(RegisterClassW(&wc)); HWND window = CreateWindowW(wc.lpszClassName, L"Authored output fixture", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr); REQUIRE(window);
    HWND foreign = CreateWindowW(wc.lpszClassName, L"Foreign fixture window", WS_OVERLAPPEDWINDOW, 0, 0, 80, 80, nullptr, nullptr, wc.hInstance, nullptr); REQUIRE(foreign);
    const auto physical = reinterpret_cast<decltype(&::GetClientRect)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetClientRect")); REQUIRE(physical);
    On12Graphics::OutputWindow output; HMODULE engine[]{GetModuleHandleW(nullptr)};
    REQUIRE(output.Initialize(window, 32, 32, 64, 48, engine, 1, Busy) == E_INVALIDARG);
    // Stay above Windows' minimum captioned-window tracking width.
    REQUIRE(SUCCEEDED(output.Initialize(window, 160, 160, 320, 320, engine, 1, Busy)));
    MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (!output.Ready()) { RECT diagnostic{}; physical(window, &diagnostic); std::fprintf(stderr, "physical size=%ld,%ld logical=%ld,%ld\n", diagnostic.right, diagnostic.bottom, logical.cx, logical.cy); }
    REQUIRE(output.Ready()); RECT actual{}, fake{}, other{}, other_actual{};
    REQUIRE(physical(window, &actual) && actual.right == 320 && actual.bottom == 320);
    REQUIRE(GetClientRect(window, &fake) && fake.right == 160 && fake.bottom == 160);
    REQUIRE(GetClientRect(foreign, &other) && physical(foreign, &other_actual) && EqualRect(&other, &other_actual));
    REQUIRE(logical.cx == 160 && logical.cy == 160);
    p = {80, 80}; REQUIRE(ClientToScreen(window, &p)); REQUIRE(ScreenToClient(window, &p) && p.x == 80 && p.y == 80);
    SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(240, 120)); REQUIRE(mouse.x == 120 && mouse.y == 60);
    RECT resized{0, 0, 200, 200}; REQUIRE(AdjustWindowRectEx(&resized, WS_OVERLAPPEDWINDOW, FALSE, 0));
    REQUIRE(SetWindowPos(window, nullptr, 0, 0, resized.right - resized.left, resized.bottom - resized.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    REQUIRE(!output.Ready() && GetClientRect(window, &actual) && actual.right == 200 && actual.bottom == 200);
    SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(120, 60)); REQUIRE(mouse.x == 120 && mouse.y == 60);
    REQUIRE(SUCCEEDED(output.Shutdown())); REQUIRE(!output.Ready());
    REQUIRE(GetClientRect(window, &actual) && actual.right == 200 && actual.bottom == 200);
    REQUIRE(GetWindowLongPtrW(window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(Procedure));
    SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(240, 120)); REQUIRE(mouse.x == 240 && mouse.y == 120);
    DestroyWindow(foreign); DestroyWindow(window);
    std::printf("{\"passed\":true,\"checks\":%u,\"logical_input_mapping\":true,\"restored\":true,\"synthetic_only\":true}\n", checks);
    return 0;
}
