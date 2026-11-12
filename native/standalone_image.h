#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <vector>

namespace StandaloneImage {
enum class Error : uint32_t {
    None, File, FileIdentity, PeIdentity, Header, Cipher, Plaintext, Mapping, Protection
};
struct SteamText {
    uint32_t rva = 0;
    uint32_t entry = 0;
    std::vector<unsigned char> bytes;
};
// Hashes are build identities, never account or platform credentials.
constexpr char SteamFile[] = "8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410";
constexpr char SteamPlaintext[] = "8448e0f29a2cca00f8bf0a74c2b242ea2eca8de7e8f331def47459fa5c086b7d";
constexpr char SteamCiphertext[] = "3b103cf17ad7616f9d7935f8448b7182d95c9fd2b427a3db56cd5ab0481b2307";
bool Digest(const void* bytes, size_t size, std::array<unsigned char, 32>& output);
bool Matches(const void* bytes, size_t size, const char* expected);
bool ReadLockedFile(HANDLE file, size_t maximum, std::vector<unsigned char>& bytes);
bool AesDecrypt(const unsigned char* key, size_t key_size, const unsigned char* input,
    size_t size, const unsigned char* iv, std::vector<unsigned char>& output);
bool DecodeSteam(const std::vector<unsigned char>& file, SteamText& output, Error& error);
bool InstallSteam(uintptr_t image, const SteamText& text, Error& error);
}
