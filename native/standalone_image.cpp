#include "standalone_image.h"
#include <bcrypt.h>
#include <cstring>
#include <limits>
#pragma comment(lib, "bcrypt.lib")

namespace StandaloneImage {
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
struct Hash {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~Hash() { if (handle) BCryptDestroyHash(handle); }
};
struct Key {
    BCRYPT_KEY_HANDLE handle = nullptr;
    ~Key() { if (handle) BCryptDestroyKey(handle); }
};
template<typename T> bool Field(const std::vector<unsigned char>& data, size_t at, T& value) {
    if (at > data.size() || data.size() - at < sizeof(T)) return false;
    memcpy(&value, data.data() + at, sizeof(T));
    return true;
}
uint32_t Word(const unsigned char* bytes) {
    uint32_t word = 0;
    memcpy(&word, bytes, sizeof(word));
    return word;
}
struct SecretHeader {
    std::array<unsigned char, 240> bytes{};
    ~SecretHeader() { SecureZeroMemory(bytes.data(), bytes.size()); }
};
}

bool Digest(const void* bytes, size_t size, std::array<unsigned char, 32>& output) {
    if ((!bytes && size) || size > (std::numeric_limits<ULONG>::max)()) return false;
    Algorithm algorithm;
    Hash hash;
    return BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
        && BCryptCreateHash(algorithm.handle, &hash.handle, nullptr, 0, nullptr, 0, 0) >= 0
        && BCryptHashData(hash.handle, const_cast<PUCHAR>(static_cast<const unsigned char*>(bytes)),
            static_cast<ULONG>(size), 0) >= 0
        && BCryptFinishHash(hash.handle, output.data(), static_cast<ULONG>(output.size()), 0) >= 0;
}
bool Matches(const void* bytes, size_t size, const char* expected) {
    if (!expected || strlen(expected) != 64) return false;
    std::array<unsigned char, 32> hash{};
    if (!Digest(bytes, size, hash)) return false;
    constexpr char hex[] = "0123456789abcdef";
    unsigned int different = 0;
    for (size_t i = 0; i < hash.size(); ++i) {
        different |= static_cast<unsigned char>(expected[2*i]) ^ static_cast<unsigned char>(hex[hash[i] >> 4]);
        different |= static_cast<unsigned char>(expected[2*i+1]) ^ static_cast<unsigned char>(hex[hash[i] & 15]);
    }
    return different == 0;
}
bool ReadLockedFile(HANDLE file, size_t maximum, std::vector<unsigned char>& bytes) {
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &before)
        || before.nFileSizeHigh || before.nFileSizeLow > maximum || !before.nFileSizeLow
        || (before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return false;
    bytes.resize(before.nFileSizeLow);
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>((bytes.size() - offset) > 1048576 ? 1048576 : bytes.size() - offset);
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr) || read != chunk) return false;
        offset += read;
    }
    return GetFileInformationByHandle(file, &after)
        && before.dwVolumeSerialNumber == after.dwVolumeSerialNumber
        && before.nFileIndexHigh == after.nFileIndexHigh && before.nFileIndexLow == after.nFileIndexLow
        && before.nFileSizeHigh == after.nFileSizeHigh && before.nFileSizeLow == after.nFileSizeLow
        && CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) == 0;
}
bool AesDecrypt(const unsigned char* key, size_t key_size, const unsigned char* input,
    size_t size, const unsigned char* iv, std::vector<unsigned char>& output) {
    output.clear();
    if (!key || key_size != 32 || !input || !size || size % 16 || size > 32*1024*1024) return false;
    Algorithm algorithm;
    Key secret;
    const wchar_t* mode = iv ? BCRYPT_CHAIN_MODE_CBC : BCRYPT_CHAIN_MODE_ECB;
    if (BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0
        || BCryptSetProperty(algorithm.handle, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
            static_cast<ULONG>((wcslen(mode)+1)*sizeof(wchar_t)), 0) < 0
        || BCryptGenerateSymmetricKey(algorithm.handle, &secret.handle, nullptr, 0,
            const_cast<PUCHAR>(key), static_cast<ULONG>(key_size), 0) < 0) return false;
    std::array<unsigned char, 16> chain{};
    if (iv) memcpy(chain.data(), iv, chain.size());
    output.resize(size);
    ULONG written = 0;
    const bool valid = BCryptDecrypt(secret.handle, const_cast<PUCHAR>(input), static_cast<ULONG>(size), nullptr,
        iv ? chain.data() : nullptr, iv ? static_cast<ULONG>(chain.size()) : 0,
        output.data(), static_cast<ULONG>(output.size()), &written, 0) >= 0 && written == size;
    SecureZeroMemory(chain.data(), chain.size());
    if (!valid) { SecureZeroMemory(output.data(), output.size()); output.clear(); }
    return valid;
}

bool DecodeSteam(const std::vector<unsigned char>& file, SteamText& output, Error& error) {
    output = SteamText{};
    error = Error::FileIdentity;
    if (file.size() != 17459856 || !Matches(file.data(), file.size(), SteamFile)) return false;
    error = Error::PeIdentity;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!Field(file, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64 || dos.e_lfanew > 4096
        || !Field(file, static_cast<size_t>(dos.e_lfanew), nt) || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt.FileHeader.TimeDateStamp != 0x5d305b24
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt.OptionalHeader.ImageBase != 0x400000
        || nt.OptionalHeader.SizeOfImage != 0x1106000 || nt.OptionalHeader.AddressOfEntryPoint != 0x10e3310
        || (nt.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)) return false;
    size_t text_offset = 0, header_offset = 0;
    const size_t table = static_cast<size_t>(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
    if (nt.FileHeader.NumberOfSections > 32) return false;
    for (size_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!Field(file, table + i*sizeof(section), section)) return false;
        if (section.VirtualAddress == 0x1000 && section.SizeOfRawData == 0xc42000
            && memcmp(section.Name, ".text\0\0\0", 8) == 0) text_offset = section.PointerToRawData;
        const uint32_t header_rva = nt.OptionalHeader.AddressOfEntryPoint - 240;
        if (header_rva >= section.VirtualAddress && header_rva-section.VirtualAddress < section.SizeOfRawData)
            header_offset = section.PointerToRawData + header_rva - section.VirtualAddress;
    }
    if (!text_offset || !header_offset || text_offset > file.size() || file.size()-text_offset < 0xc42000
        || header_offset > file.size() || file.size()-header_offset < 240) return false;
    error = Error::Header;
    SecretHeader header;
    memcpy(header.bytes.data(), file.data()+header_offset, header.bytes.size());
    uint32_t previous = Word(header.bytes.data());
    for (size_t i = 4; i < header.bytes.size(); i += 4) {
        const uint32_t encoded = Word(header.bytes.data()+i);
        const uint32_t decoded = encoded ^ previous;
        memcpy(header.bytes.data()+i, &decoded, 4);
        previous = encoded;
    }
    if (!Matches(header.bytes.data(), header.bytes.size(), "508359e2c25b1ced95cf1bdf541d35fd39a837389cc2300d9cffd35516bdf67c")) return false;
    // Only data from the user's exact file supplies the key and cipher blocks.
    // This implementation contains neither embedded game bytes nor key material.
    error = Error::Cipher;
    std::vector<unsigned char> iv, cipher(16 + 0xc42000), decoded;
    if (!AesDecrypt(header.bytes.data()+88, 32, header.bytes.data()+120, 16, nullptr, iv)) return false;
    memcpy(cipher.data(), header.bytes.data()+136, 16);
    memcpy(cipher.data()+16, file.data()+text_offset, 0xc42000);
    const bool decrypted = AesDecrypt(header.bytes.data()+88, 32, cipher.data(), cipher.size(), iv.data(), decoded);
    SecureZeroMemory(iv.data(), iv.size());
    if (!decrypted) return false;
    decoded.resize(0xc42000);
    error = Error::Plaintext;
    if (!Matches(decoded.data(), decoded.size(), SteamPlaintext)) return false;
    output.rva = 0x1000;
    output.entry = 0x622537;
    output.bytes.swap(decoded);
    error = Error::None;
    return true;
}

bool InstallSteam(uintptr_t image, const SteamText& text, Error& error) {
    error = Error::Mapping;
    if (image != 0x400000 || text.rva != 0x1000 || text.entry != 0x622537 || text.bytes.size() != 0xc42000
        || !Matches(text.bytes.data(), text.bytes.size(), SteamPlaintext)) return false;
    auto* target = reinterpret_cast<unsigned char*>(image+text.rva);
    size_t offset = 0;
    while (offset < text.bytes.size()) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(target+offset, &region, sizeof(region)) != sizeof(region)
            || region.State != MEM_COMMIT || region.Type != MEM_IMAGE || region.AllocationBase != reinterpret_cast<void*>(image)
            || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const size_t step = region.RegionSize - (reinterpret_cast<uintptr_t>(target+offset)-reinterpret_cast<uintptr_t>(region.BaseAddress));
        if (!step) return false;
        offset += step > text.bytes.size()-offset ? text.bytes.size()-offset : step;
    }
    if (!Matches(target, text.bytes.size(), SteamCiphertext)) return false;
    error = Error::Protection;
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(target, text.bytes.size(), PAGE_READWRITE, &old)) return false;
    memcpy(target, text.bytes.data(), text.bytes.size());
    const bool valid = FlushInstructionCache(GetCurrentProcess(), target, text.bytes.size())
        && VirtualProtect(target, text.bytes.size(), PAGE_EXECUTE_READ, &ignored)
        && Matches(target, text.bytes.size(), SteamPlaintext);
    if (!valid) return false; // Caller must fail closed before any game entry.
    error = Error::None;
    return true;
}
}
