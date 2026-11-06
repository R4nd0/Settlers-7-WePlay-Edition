// Real CNG and strict-format checks with an ephemeral synthetic P-256 key.
// No game file, local identity or persisted private key is used.
#include "offline_bootstrap.cpp"
#include <cstdio>
#include <string>
void ReportLocalStatus(const char*) {}

int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool okay) { ++checks; if (!okay) ++failures; };
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0);
    if (!algorithm) return 1;
    check(BCryptGenerateKeyPair(algorithm, &key, 256, 0) >= 0);
    if (!key) { BCryptCloseAlgorithmProvider(algorithm, 0); return 1; }
    check(BCryptFinalizeKeyPair(key, 0) >= 0);
    unsigned char blob[72]{}; ULONG size = sizeof(blob);
    check(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob, sizeof(blob), &size, 0) >= 0 && size == sizeof(blob));
    BCryptDestroyKey(key); BCryptCloseAlgorithmProvider(algorithm, 0);
    auto encode = [&]() {
        char result[97]{}; DWORD length = sizeof(result);
        bool okay = CryptBinaryToStringA(blob, sizeof(blob), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result, &length) != FALSE;
        check(okay && strnlen_s(result, sizeof(result)) == 96);
        return std::string(result);
    };
    const std::string valid = encode();
    check(ValidateKey(valid.c_str()));
    check(!ValidateKey(nullptr));
    check(!ValidateKey(""));
    check(!ValidateKey(valid.substr(0, 95).c_str()));
    check(!ValidateKey((valid + "A").c_str()));
    for (char bad : {'=', ' ', '\r', '\n', '-', '_', '\0', static_cast<char>(0xff)}) {
        auto changed = valid; changed[48] = bad;
        check(!ValidateKey(changed.c_str()));
    }
    blob[0] = 'X'; check(!ValidateKey(encode().c_str())); blob[0] = 'E';
    blob[4] = 31; check(!ValidateKey(encode().c_str())); blob[4] = 32;
    for (size_t i = 8; i < sizeof(blob); ++i) blob[i] = 0;
    check(!ValidateKey(encode().c_str()));
    std::printf("%u public-key CNG/canonical-format checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
