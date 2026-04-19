#pragma once

#include "Crypto/PRNG.h"
#include <util.h>

#include <vector>
#include <functional>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <stdexcept>

using namespace osuCrypto;



#include "../thirdparty/KeccakTools/Sources/Keccak-f.h"

using Bits = std::vector<uint8_t>;   // each entry must be 0 or 1

#define KEM_key_block_size 100 //1600*8/128 byte
#define KEM_key_size_bit 12800 //1600*8/128 byte
#define  Keccak_size_bit 1600 //bits

typedef std::array<block, KEM_key_block_size> kemKey;


inline Bits KemKeyToBits(const std::array<block, KEM_key_block_size>& key)
{
    constexpr size_t BLOCK_BITS = 128; // adjust if needed
    Bits bits;
    bits.reserve(KEM_key_block_size * BLOCK_BITS);

    for (const auto& b : key) {
        // Treat block as 128-bit value
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&b);

        for (size_t i = 0; i < 16; ++i) {          // 16 bytes per block
            for (size_t j = 0; j < 8; ++j) {       // 8 bits per byte
                bits.push_back((bytes[i] >> j) & 1U);
            }
        }
    }

    return bits;
}

inline std::array<block, KEM_key_block_size> BitsToKemKey(const Bits& bits)
{
    constexpr size_t BLOCK_BITS = 128;
    if (bits.size() != KEM_key_block_size * BLOCK_BITS) {
        throw std::invalid_argument("Wrong bit size");
    }

    std::array<block, KEM_key_block_size> key{};

    size_t idx = 0;

    for (size_t k = 0; k < KEM_key_block_size; ++k) {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(&key[k]);

        for (size_t i = 0; i < 16; ++i) {
            uint8_t byte = 0;
            for (size_t j = 0; j < 8; ++j) {
                byte |= (bits[idx++] & 1U) << j;
            }
            bytes[i] = byte;
        }
    }

    return key;
}

namespace Keccak1600Adapter {

    static constexpr size_t KECCAK_BITS = 1600;
    static constexpr size_t KECCAK_BYTES = KECCAK_BITS / 8;

    inline std::array<UINT8, KECCAK_BYTES> packBits(const Bits& x) {
        if (x.size() != KECCAK_BITS) {
            throw std::invalid_argument("Keccak1600Adapter::packBits: input must be 1600 bits.");
        }

        std::array<UINT8, KECCAK_BYTES> buf{};
        buf.fill(0);

        for (size_t i = 0; i < KECCAK_BITS; ++i) {
            if (x[i] & 1U) {
                buf[i / 8] |= static_cast<UINT8>(1U << (i % 8));
            }
        }
        return buf;
    }

    inline Bits unpackBits(const std::array<UINT8, KECCAK_BYTES>& buf) {
        Bits x(KECCAK_BITS, 0);

        for (size_t i = 0; i < KECCAK_BITS; ++i) {
            x[i] = static_cast<uint8_t>((buf[i / 8] >> (i % 8)) & 1U);
        }
        return x;
    }

    inline const KeccakF& getKeccak1600()
    {
        static const KeccakF keccak1600(1600);
        return keccak1600;
    }

    inline Bits pi(const Bits& x) {
        auto buf = packBits(x);
        getKeccak1600()(buf.data());
        return unpackBits(buf);
    }

    inline Bits pi_inv(const Bits& x) {
        auto buf = packBits(x);
        getKeccak1600().inverse(buf.data());
        return unpackBits(buf);
    }

} // namespace Keccak1600Adapter

class ConstructionPermutation {
public:
    // pi and pi_inv act only on the first n bits
    ConstructionPermutation(
        size_t n_bits,
        size_t N_bits,
        size_t s_bits,
        std::function<Bits(const Bits&)> pi_func,
        std::function<Bits(const Bits&)> pi_inv_func
    )
        : n(n_bits), N(N_bits), s(s_bits),
        pi(std::move(pi_func)), pi_inv(std::move(pi_inv_func)) {

        if (n == 0 || N == 0 || s == 0) {
            throw std::invalid_argument("n, N, and s must be positive.");
        }
        if (N <= n) {
            throw std::invalid_argument("Need N > n.");
        }
        if ((N - n) % s != 0) {
            throw std::invalid_argument("s must divide (N - n).");
        }
        if (s > n) {
            throw std::invalid_argument("Need s <= n so the range [s+1..n] is non-empty.");
        }

        t = (N - n) / s + 1;
        r = 5 * t;
        hasRoundXor = (n > s);
    }

    Bits encrypt(Bits X) const {
        check_state(X);

        // for (size_t i = 1; i <= r - 1; ++i) {
        //     apply_pi_to_first_n(X);
        //     xor_round_index_into_slice(X, i);
        //     rotate_left(X, s);
        // }
        // apply_pi_to_first_n(X);

        // shift view, no full rotate per round
        size_t shift = 0;
        Bits prefix;
        prefix.resize(n);

        for (size_t i = 1; i <= r - 1; ++i) {
            apply_pi_to_first_n_shifted(X, shift, prefix);
            // skip empty xor step when n==s
            if (hasRoundXor) xor_round_index_into_slice_shifted(X, shift, i);
            shift = (shift + s) % N;
        }

        apply_pi_to_first_n_shifted(X, shift, prefix);
        materialize_shifted_state(X, shift);
        return X;
    }

    Bits decrypt(Bits X) const {
        check_state(X);

        // kept for reference
        // apply_pi_inv_to_first_n(X);
        // for (size_t i = r - 1; i >= 1; --i) {
        //     rotate_right(X, s);
        //     xor_round_index_into_slice(X, i);
        //     apply_pi_inv_to_first_n(X);
        //     if (i == 1) break; // avoid size_t underflow
        // }

        // reverse with the same shift view
        size_t shift = 0;
        Bits prefix;
        prefix.resize(n);

        apply_pi_inv_to_first_n_shifted(X, shift, prefix);

        for (size_t i = r - 1; i >= 1; --i) {
            shift = (shift + N - s) % N;
            // skip empty xor step when n==s
            if (hasRoundXor) xor_round_index_into_slice_shifted(X, shift, i);
            apply_pi_inv_to_first_n_shifted(X, shift, prefix);

            if (i == 1) break; // avoid size_t underflow
        }

        materialize_shifted_state(X, shift);
        return X;
    }

    size_t rounds() const { return r; }

private:
    size_t n, N, s, t, r;
    bool hasRoundXor = true;
    std::function<Bits(const Bits&)> pi;
    std::function<Bits(const Bits&)> pi_inv;

    void check_state(const Bits& X) const {
        if (X.size() != N) {
            throw std::invalid_argument("State length must be exactly N bits.");
        }
        for (uint8_t b : X) {
            if (b != 0 && b != 1) {
                throw std::invalid_argument("State must contain only 0/1 values.");
            }
        }
    }

    size_t shifted_idx(size_t i, size_t shift) const {
        // logical index -> physical index
        const size_t j = i + shift;
        return (j < N) ? j : (j - N);
    }

    void materialize_shifted_state(Bits& X, size_t shift) const {
        if (shift == 0) return;
        // materialize shifted view
        std::rotate(X.begin(), X.begin() + shift, X.end());
    }

    void apply_pi_to_first_n(Bits& X) const {
        // Bits prefix(X.begin(), X.begin() + n);
        // prefix = pi(prefix);
        // if (prefix.size() != n) {
        //     throw std::runtime_error("pi must return exactly n bits.");
        // }
        // std::copy(prefix.begin(), prefix.end(), X.begin());

        // reuse one local buffer
        thread_local Bits prefix;
        prefix.resize(n);
        std::copy_n(X.begin(), n, prefix.begin());

        prefix = pi(prefix);
        if (prefix.size() != n) {
            throw std::runtime_error("pi must return exactly n bits.");
        }
        std::copy_n(prefix.begin(), n, X.begin());
    }

    void apply_pi_inv_to_first_n(Bits& X) const {
        // Bits prefix(X.begin(), X.begin() + n);
        // prefix = pi_inv(prefix);
        // if (prefix.size() != n) {
        //     throw std::runtime_error("pi_inv must return exactly n bits.");
        // }
        // std::copy(prefix.begin(), prefix.end(), X.begin());

        // reuse one local buffer
        thread_local Bits prefix;
        prefix.resize(n);
        std::copy_n(X.begin(), n, prefix.begin());

        prefix = pi_inv(prefix);
        if (prefix.size() != n) {
            throw std::runtime_error("pi_inv must return exactly n bits.");
        }
        std::copy_n(prefix.begin(), n, X.begin());
    }

    void apply_pi_to_first_n_shifted(Bits& X, size_t shift, Bits& prefix) const {
        // for (size_t i = 0; i < n; ++i) {
        //     prefix[i] = X[shifted_idx(i, shift)];
        // }

        // two contiguous copies
        const size_t firstLen = std::min(n, N - shift);
        std::copy_n(X.begin() + shift, firstLen, prefix.begin());
        if (firstLen < n) {
            std::copy_n(X.begin(), n - firstLen, prefix.begin() + firstLen);
        }

        prefix = pi(prefix);
        if (prefix.size() != n) {
            throw std::runtime_error("pi must return exactly n bits.");
        }

        // for (size_t i = 0; i < n; ++i) {
        //     X[shifted_idx(i, shift)] = prefix[i];
        // }
        std::copy_n(prefix.begin(), firstLen, X.begin() + shift);
        if (firstLen < n) {
            std::copy_n(prefix.begin() + firstLen, n - firstLen, X.begin());
        }
    }

    void apply_pi_inv_to_first_n_shifted(Bits& X, size_t shift, Bits& prefix) const {
        // for (size_t i = 0; i < n; ++i) {
        //     prefix[i] = X[shifted_idx(i, shift)];
        // }

        // two contiguous copies
        const size_t firstLen = std::min(n, N - shift);
        std::copy_n(X.begin() + shift, firstLen, prefix.begin());
        if (firstLen < n) {
            std::copy_n(X.begin(), n - firstLen, prefix.begin() + firstLen);
        }

        prefix = pi_inv(prefix);
        if (prefix.size() != n) {
            throw std::runtime_error("pi_inv must return exactly n bits.");
        }

        // for (size_t i = 0; i < n; ++i) {
        //     X[shifted_idx(i, shift)] = prefix[i];
        // }
        std::copy_n(prefix.begin(), firstLen, X.begin() + shift);
        if (firstLen < n) {
            std::copy_n(prefix.begin() + firstLen, n - firstLen, X.begin());
        }
    }

    // XOR the round counter i into X[s .. n-1] (0-based indexing)
    // This corresponds to X[s+1..n] in the paper's 1-based indexing.
    void xor_round_index_into_slice(Bits& X, size_t i) const {
        size_t len = n - s;
        for (size_t bit = 0; bit < len; ++bit) {
            X[s + bit] ^= static_cast<uint8_t>((i >> bit) & 1U);
        }
    }

    void xor_round_index_into_slice_shifted(Bits& X, size_t shift, size_t i) const {
        size_t len = n - s;
        for (size_t bit = 0; bit < len; ++bit) {
            X[shifted_idx(s + bit, shift)] ^= static_cast<uint8_t>((i >> bit) & 1U);
        }
    }

    // Cyclic shift left by k bits on the whole N-bit state
    void rotate_left(Bits& X, size_t k) const {
        k %= N;
        if (k == 0) return;

        // Bits tmp = X;
        // for (size_t i = 0; i < N; ++i) {
        //     X[i] = tmp[(i + k) % N];
        // }

        // in-place rotate
        std::rotate(X.begin(), X.begin() + k, X.end());
    }

    // Cyclic shift right by k bits on the whole N-bit state
    void rotate_right(Bits& X, size_t k) const {
        k %= N;
        if (k == 0) return;

        // Bits tmp = X;
        // for (size_t i = 0; i < N; ++i) {
        //     X[(i + k) % N] = tmp[i];
        // }

        // in-place rotate
        std::rotate(X.begin(), X.end() - k, X.end());
    }
};


inline int permutation_Test() {
    size_t n = 1600;
    size_t N = n * 8;
    size_t s = n;

    ConstructionPermutation P(n, N, s, Keccak1600Adapter::pi, Keccak1600Adapter::pi_inv);

    Bits X(N, 0);
    X[0] = 1;
    X[5] = 1;

    Bits Y = P.encrypt(X);
    Bits Z = P.decrypt(Y);

    return 0;
}
