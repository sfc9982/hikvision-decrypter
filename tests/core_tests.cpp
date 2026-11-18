#include "hikvision.h"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void Rejects(const std::function<void()>& operation)
    {
        bool rejected = false;
        try { operation(); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "invalid input was not rejected");
    }
}

int main()
{
    int failures = 0;
    int cases = 0;
    auto Run = [&](const char* name, const std::function<void()>& test) {
        ++cases;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    Run("AES-128 independent known-answer vector", [] {
        unsigned char key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        unsigned char plain[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        const unsigned char expected[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
        AES aes(128); unsigned int length = 0;
        std::unique_ptr<unsigned char[]> cipher(aes.EncryptECB(plain, 16, key, length));
        Require(length == 16 && std::equal(cipher.get(), cipher.get() + 16, expected), "AES encryption mismatch");
        std::unique_ptr<unsigned char[]> decoded(aes.DecryptECB(cipher.get(), length, key));
        Require(std::equal(decoded.get(), decoded.get() + 16, plain), "AES decryption mismatch");
    });
    Run("XOR uses explicit four-byte period on x64", [] {
        const unsigned char key[4] = {0x73, 0x8b, 0x55, 0x44};
        unsigned char bytes[8] = {};
        const unsigned char expected[8] = {0x73,0x8b,0x55,0x44,0x73,0x8b,0x55,0x44};
        decryptXOR(bytes, sizeof(bytes), key, sizeof(key));
        Require(std::equal(bytes, bytes + 8, expected), "XOR key period mismatch");
        Rejects([&] { decryptXOR(bytes, sizeof(bytes), key, 0); });
        Rejects([&] { decryptXOR(bytes, sizeof(bytes), nullptr, 4); });
        Rejects([&] { decryptXOR(nullptr, 1, key, 4); });
    });
    Run("combined format matches independent OpenSSL fixture", [] {
        std::vector<unsigned char> input(32);
        for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<unsigned char>(i);
        const std::vector<unsigned char> expected = {
            0xc5,0x2c,0x24,0xc7,0x52,0x97,0x94,0x1e,0x9d,0x39,0xa2,0xa1,0x7b,0x34,0x80,0xfe,
            0x5a,0x36,0xfc,0xe8,0x3f,0x1b,0xcf,0x51,0x8c,0x6c,0xa8,0x81,0xb4,0x21,0xae,0x5c};
        const auto original = input;
        Require(transformConfiguration(input, ConversionMode::Encrypt) == expected, "combined ciphertext mismatch");
        Require(transformConfiguration(expected, ConversionMode::Decrypt) == input, "combined plaintext mismatch");
        Require(input == original, "input was modified");
    });
    Run("empty and truncated ciphertext is rejected", [] {
        Rejects([] { transformConfiguration({}, ConversionMode::Encrypt); });
        for (size_t length : {0u, 1u, 15u, 17u, 31u})
            Rejects([&] { transformConfiguration(std::vector<unsigned char>(length), ConversionMode::Decrypt); });
    });
    Run("legacy non-aligned padding is preserved without trimming", [] {
        const unsigned char xorKey[4] = {0x73,0x8b,0x55,0x44};
        for (size_t length : {1u, 15u, 16u, 17u, 31u, 32u})
        {
            const std::vector<unsigned char> plain(length, 0);
            const auto cipher = transformConfiguration(plain, ConversionMode::Encrypt);
            Require(cipher.size() == ((length + 15) / 16) * 16, "unexpected padded length");
            const auto decoded = transformConfiguration(cipher, ConversionMode::Decrypt);
            Require(std::equal(plain.begin(), plain.end(), decoded.begin()), "original bytes changed");
            for (size_t i = length; i < decoded.size(); ++i)
                Require(decoded[i] == xorKey[i % 4], "legacy padding semantics changed");
        }
    });
    Run("legacy AES wrappers reject invalid buffers and lengths", [] {
        unsigned char key[16] = {};
        auto* bytes = new unsigned char[17]{};
        try
        {
            Rejects([&] { decryptAES(bytes, 17, key); });
            Rejects([&] { decryptAES(bytes, 0, key); });
            size_t length = std::numeric_limits<unsigned int>::max();
            Rejects([&] { encryptAES(bytes, length, key); });
        }
        catch (...) { delete[] bytes; throw; }
        delete[] bytes;
    });
    std::cout << cases - failures << "/" << cases << " test groups passed\n";
    return failures == 0 ? 0 : 1;
}
