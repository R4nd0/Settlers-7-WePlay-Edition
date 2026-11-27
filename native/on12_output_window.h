#pragma once
#include <windows.h>
#include <cstddef>

namespace On12Graphics {
// A single owned window. Only imports of the pinned engine modules see the
// logical render extent; D3D9/DXGI and the desktop see the physical output.
class OutputWindow {
public:
    HRESULT Initialize(HWND, UINT render_width, UINT render_height, UINT output_width, UINT output_height,
                       const HMODULE* engine_modules, size_t count, bool (*inside_render_call)());
    bool Ready() const;
    HRESULT Shutdown();
};
bool ScalePoint(POINT&, UINT from_width, UINT from_height, UINT to_width, UINT to_height);
}
