#pragma once
#include <cstddef>
#include <cstdint>
namespace On12WaterCompat {
bool Eligible();
bool Vertex(const void*,std::size_t);
bool Pixel(const void*,std::size_t);
void DeviceReady();
void Converted();
void Observe(bool,const std::uint32_t*,const std::uint32_t*);
}
