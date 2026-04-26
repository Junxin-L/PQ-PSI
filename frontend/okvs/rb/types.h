#pragma once

#include "Crypto/PRNG.h"
#include "Common/Defines.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace osuCrypto
{
    using RBBits = std::array<u64, 4>;

    struct RBParams
    {
        size_t lambda = 40;
        double eps = 0.0;
        size_t columns = 0;
        size_t bandWidth = 0;
        bool multiThread = true;
        block startSeed = toBlock(0x9b3a7f21ULL, 0x6c8e1d44ULL);
        block maskSeed = toBlock(0x4f12c0deULL, 0xa55a91b7ULL);
    };

    struct RBInfo
    {
        size_t n = 0;
        size_t lambda = 40;
        double lambdaReal = 0.0;
        double eps = 0.0;
        size_t columns = 0;
        size_t bandWidth = 0;
    };

    struct RBRow
    {
        size_t start = 0;
        RBBits bits{};
        std::vector<block> rhs;
    };
}
