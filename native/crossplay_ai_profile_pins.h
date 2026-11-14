#pragma once
#include <cstdint>
// Expected original runtime method bodies. Steam's encrypted on-disk bytes
// are not executable method fixtures; runtime integrity remains mandatory.
namespace CrossplayAIProfileGuards {
struct Region {uintptr_t address;unsigned bytes;uint64_t hash;};
inline constexpr Region Pins[3][8]={
    {{0xf4e7d8,24,0xa67382c082d12242ull},{0x99cdb0,33,0x4f7c8279929f5c8cull},{0xa48650,6,0x195d357afd13e6a0ull},{0x4244d0,5,0x3e189c728b0c65e4ull},{0xa49be0,148,0xab26939e879bde15ull},{0xa48660,6,0x31b5d0af35a4361full},{0x99cd60,113,0xf174279ab80b8f4dull},{0xa494f0,148,0xc47352bfee3d0219ull}},
    {{0x11408e4,24,0x6bb03711ffc47858ull},{0x690ea0,46,0x82e6a0f4ef5b4874ull},{0xee4b50,532,0x526ecbd13b0cb2dcull},{0x4eeec0,5,0x3e189c728b0c65e4ull},{0xee46c0,52,0xbf2de6796838ca11ull},{0xee4b40,548,0x4eb7364e7088e5ccull},{0x69b9a0,53,0x73ae7cc296ef6427ull},{0x69c440,199,0x137c64190856135dull}},
    {{0x113eeb4,24,0xb7ae9a6fc8a6f646ull},{0x690a80,46,0x6d693759a7185ec3ull},{0xee4240,532,0xcf694c600cb56ef5ull},{0x4eebe0,5,0x3e189c728b0c65e4ull},{0xee3db0,52,0x594c8ed32d45678dull},{0xee4230,548,0xc315d05529187745ull},{0x69b580,53,0x40018daefff0ec30ull},{0x69c020,199,0x433692a8504ecc02ull}},
};
inline constexpr uintptr_t MutexTables[3]={0xf30dcc,0x11380b0,0x11365a8};
inline constexpr uintptr_t Tables[3]={0xf4e7d8,0x11408e4,0x113eeb4};
}
