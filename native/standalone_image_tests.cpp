#include "standalone_image.h"
#include <cstdio>
#include <cstring>

int wmain(int argc, wchar_t** argv) {
    unsigned int checks = 0;
    auto check = [&](bool valid) { ++checks; if (!valid) { std::printf("FAIL image check %u\n", checks); return false; } return true; };
    if (!check(StandaloneImage::Matches("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
        || !check(!StandaloneImage::Matches("abd", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))) return 1;
    // Independent AES-256 ECB known-answer vector; no game key or game bytes.
    const unsigned char key[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    const unsigned char cipher[] = {0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89};
    const unsigned char plain[] = {0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    std::vector<unsigned char> result;
    if (!check(StandaloneImage::AesDecrypt(key,32,cipher,16,nullptr,result) && result.size()==16 && !memcmp(result.data(),plain,16))
        || !check(!StandaloneImage::AesDecrypt(key,31,cipher,16,nullptr,result))
        || !check(!StandaloneImage::AesDecrypt(key,32,cipher,15,nullptr,result))) return 1;
    StandaloneImage::SteamText text;
    StandaloneImage::Error error{};
    if (!check(!StandaloneImage::DecodeSteam({},text,error) && error==StandaloneImage::Error::FileIdentity)
        || !check(!StandaloneImage::InstallSteam(0,text,error))) return 1;
    if (argc == 2) {
        HANDLE file = CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        std::vector<unsigned char> raw;
        const bool read = StandaloneImage::ReadLockedFile(file, 32*1024*1024, raw);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (!check(read) || !check(StandaloneImage::DecodeSteam(raw,text,error))
            || !check(text.entry==0x622537 && text.rva==0x1000 && text.bytes.size()==0xc42000)) return 1;
        raw[1000] ^= 1;
        if (!check(!StandaloneImage::DecodeSteam(raw,text,error) && error==StandaloneImage::Error::FileIdentity)) return 1;
        std::puts("Exact original Steam file decoded and full plaintext digest verified; no game launched or file changed.");
    }
    std::printf("PASS %u standalone image checks\n", checks);
    return 0;
}
