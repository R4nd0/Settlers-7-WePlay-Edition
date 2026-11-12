#pragma once
namespace CrossplayClosedSlot {
struct PinRange {uintptr_t address;size_t length;};
// History: complete closed-slot dispatch path and selector tables; unrelated
// resume-fragment branches have their own adapter guards. No byte masks.
inline constexpr PinRange Pins[3][16]={{{0xf4df88,24},{0x98cb90,32},{0xa48510,32},{0x4244d0,32},{0xa494f0,32},{0xa48520,32},{0x98cb40,32},{0xa494f0,148},{0x98cb40,68},{0x0,0},{0x0,0},{0x0,0},{0x0,0},{0x0,0},{0x0,0},{0x0,0}},
{{0x1140488,24},{0x65dc10,32},{0xee4d20,32},{0x4eeec0,32},{0xee4d30,32},{0xee4d70,32},{0x690d90,32},{0xee4d30,52},{0x690d90,53},{0x841060,78},{0x841281,186},{0x841bc4,32},{0x841be4,127},{0x65dbb0,17},{0x690d60,44},{0x690dd0,49}},
{{0x113e9cc,24},{0x65d7f0,32},{0xee4410,32},{0x4eebe0,32},{0xee4420,32},{0xee4460,32},{0x690970,32},{0xee4420,52},{0x690970,53},{0x840e30,78},{0x841051,186},{0x841994,32},{0x8419b4,127},{0x65d790,17},{0x690940,44},{0x6909b0,49}}};
inline constexpr uint64_t Hashes[]={0x83316c28fd8c4f80ull,0x2e37f1cde97e9618ull,0x1de480b19ee2455dull};
}
