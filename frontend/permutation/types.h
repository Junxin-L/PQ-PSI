#pragma once

#include "Common/Defines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace osuCrypto;

using Bits = std::vector<uint8_t>;

// ML-KEM-512 needs at most 800 bytes in one protocol row:
// raw pk = 800 bytes, and ciphertext + shared secret = 768 + 32 bytes.
// the permutation/OKVS payload to a larger state.
constexpr size_t KEM_key_block_size = 50;
constexpr size_t KEM_key_size_bit = KEM_key_block_size * sizeof(block) * 8;
constexpr size_t Keccak_size_bit = 1600;

using kemKey = std::array<block, KEM_key_block_size>;
