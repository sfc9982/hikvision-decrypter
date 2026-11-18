#pragma once
#include <cstddef>
#include <vector>
#include "AES.h"

enum class ConversionMode { Decrypt, Encrypt };

std::vector<unsigned char> transformConfiguration(const std::vector<unsigned char>& input, ConversionMode mode);
void decryptAES(unsigned char*&, const size_t, unsigned char*);
void encryptAES(unsigned char*&, size_t&, unsigned char*);
void decryptXOR(unsigned char*, const size_t, const unsigned char*, const size_t keyLength);
