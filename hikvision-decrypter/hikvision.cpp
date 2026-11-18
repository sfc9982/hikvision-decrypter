#include "hikvision.h"
#include <limits>
#include <memory>
#include <stdexcept>

void decryptAES(unsigned char*& bytes, const size_t length, unsigned char* key)
{
    if (!bytes || !key || length == 0 || length % 16 != 0 || length > std::numeric_limits<unsigned int>::max())
        throw std::invalid_argument("Invalid AES ciphertext length or buffer");
	AES aes(128);
	unsigned char* result = aes.DecryptECB(bytes, static_cast<unsigned int>(length), key);

	delete[] bytes;
	bytes = result;
}

void encryptAES(unsigned char*& bytes, size_t& length, unsigned char* key)
{
    if (!bytes || !key || length == 0 || length > std::numeric_limits<unsigned int>::max() - 15u)
        throw std::invalid_argument("Invalid AES plaintext length or buffer");
	AES aes(128);
	unsigned int newLength = 0;
	unsigned char* result = aes.EncryptECB(bytes, static_cast<unsigned int>(length), key, newLength);

	length = newLength;
	delete[] bytes;
	bytes = result;
}

void decryptXOR(unsigned char* bytes, const size_t length, const unsigned char* key, const size_t keyLength)
{
    if (!key || keyLength == 0 || (!bytes && length != 0))
        throw std::invalid_argument("Invalid XOR key or buffer");

	for (size_t i = 0; i < length; i++) {
		bytes[i] ^= key[i % keyLength];
	}
}


std::vector<unsigned char> transformConfiguration(const std::vector<unsigned char>& input, ConversionMode mode)
{
    if (input.empty())
        throw std::invalid_argument("The input file is empty");
    if (input.size() > std::numeric_limits<unsigned int>::max() - 15u)
        throw std::length_error("The input exceeds the supported AES size");
    if (mode == ConversionMode::Decrypt && input.size() % 16 != 0)
        throw std::invalid_argument("Ciphertext must contain complete 16-byte blocks");

    unsigned char aesKey[16] = { 0x27, 0x99, 0x77, 0xf6, 0x2f, 0x6c, 0xfd, 0x2d,
                                 0x91, 0xcd, 0x75, 0xb8, 0x89, 0xce, 0x0c, 0x9a };
    const unsigned char xorKey[4] = { 0x73, 0x8b, 0x55, 0x44 };
    AES aes(128);
    std::vector<unsigned char> working(input);
    const auto length = static_cast<unsigned int>(working.size());
    if (mode == ConversionMode::Encrypt)
    {
        decryptXOR(working.data(), working.size(), xorKey, sizeof(xorKey));
        unsigned int resultLength = 0;
        std::unique_ptr<unsigned char[]> result(aes.EncryptECB(working.data(), length, aesKey, resultLength));
        return std::vector<unsigned char>(result.get(), result.get() + resultLength);
    }
    std::unique_ptr<unsigned char[]> result(aes.DecryptECB(working.data(), length, aesKey));
    decryptXOR(result.get(), length, xorKey, sizeof(xorKey));
    return std::vector<unsigned char>(result.get(), result.get() + length);
}
