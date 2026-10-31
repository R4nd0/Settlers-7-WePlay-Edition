#include "castle_wire.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks = 0;
void Check(bool value) { ++checks; if (!value) exit(1); }
int main() {
    using namespace castle_wire;
    Design original{"CF_Castle_01", {{"CF_Base_03", UINT32_MAX, 0}, {"CF_Turret_01", 0, 7}}}, decoded;
    std::vector<unsigned char> bytes;
    Check(Encode(original, bytes)); Check(Decode(bytes, decoded));
    Check(decoded.type == original.type && decoded.parts.size() == 2 && decoded.parts[1].asset == original.parts[1].asset);
    const auto good = bytes;
    for (size_t n = 0; n < good.size(); ++n) {
        bytes.assign(good.begin(), good.begin() + n); Check(!Decode(bytes, decoded)); Check(decoded.parts.empty());
    }
    bytes = good; bytes.push_back(0); Check(!Decode(bytes, decoded));
    bytes = good; bytes[0] ^= 1; Check(!Decode(bytes, decoded));
    bytes = good; bytes[4] = 255; bytes[5] = 255; Check(!Decode(bytes, decoded));
    Check(Encode({}, bytes) && Decode(bytes, decoded) && decoded.parts.empty());
    for (const char* bad : {"../evil", "C:/evil", "asset\\evil", "asset/evil", "", "space asset"}) {
        auto invalid = original; invalid.parts[0].asset = bad; Check(!Encode(invalid, bytes) && bytes.empty());
    }
    auto invalid = original; invalid.parts[1].parent = 1; Check(!Encode(invalid, bytes));
    invalid = original; invalid.parts[1].parent = 5; Check(!Encode(invalid, bytes));
    invalid = original; invalid.parts[0].slot = 1025; Check(!Encode(invalid, bytes));
    invalid = original; invalid.type.assign(MaxAsset + 1, 'A'); Check(!Encode(invalid, bytes));
    invalid = original; invalid.parts.resize(MaxParts + 1); Check(!Encode(invalid, bytes));
    invalid = original; invalid.parts.clear(); Check(!Encode(invalid, bytes));
    invalid = original; invalid.parts.clear();
    for (uint32_t i = 0; i < MaxParts; ++i) invalid.parts.push_back({std::string(MaxAsset, 'A'), i ? i - 1 : UINT32_MAX, 0});
    Check(!Encode(invalid, bytes) && bytes.empty());
    printf("%u castle wire checks passed.\n", checks);
}
