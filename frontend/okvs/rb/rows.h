#pragma once

#include "fit.h"
#include "parallel.h"

namespace osuCrypto
{
    struct RBHashCtx
    {
        AES start;
        AES mask0;
        AES mask1;
    };

    inline RBHashCtx RBMakeHashCtx(const RBParams& params)
    {
        RBHashCtx ctx;
        ctx.start.setKey(params.startSeed);
        ctx.mask0.setKey(params.maskSeed ^ toBlock(0, 1));
        ctx.mask1.setKey(params.maskSeed ^ toBlock(0, 2));
        return ctx;
    }

    inline u64 RBHash64(block key, block seed)
    {
        AES h;
        h.setKey(seed);
        block x = h.ecbEncBlock(key);
        const u64* w = reinterpret_cast<const u64*>(&x);
        return w[0] ^ w[1];
    }

    inline u64 RBHash64(block key, const AES& h)
    {
        block x = h.ecbEncBlock(key);
        const u64* w = reinterpret_cast<const u64*>(&x);
        return w[0] ^ w[1];
    }

    inline std::array<u64, 4> RBMask256(block key, block seed)
    {
        std::array<u64, 4> out{};
        for (u64 i = 0; i < 2; ++i)
        {
            AES h;
            h.setKey(seed ^ toBlock(0, i + 1));
            block x = h.ecbEncBlock(key);
            const u64* w = reinterpret_cast<const u64*>(&x);
            out[2 * i + 0] = w[0];
            out[2 * i + 1] = w[1];
        }
        return out;
    }

    inline std::array<u64, 4> RBMask256(block key, const RBHashCtx& ctx)
    {
        std::array<u64, 4> out{};
        block x0 = ctx.mask0.ecbEncBlock(key);
        block x1 = ctx.mask1.ecbEncBlock(key);
        const u64* w0 = reinterpret_cast<const u64*>(&x0);
        const u64* w1 = reinterpret_cast<const u64*>(&x1);
        out[0] = w0[0];
        out[1] = w0[1];
        out[2] = w1[0];
        out[3] = w1[1];
        return out;
    }

    inline void RBTrimBits(RBBits& bits, size_t width)
    {
        if (width > 256)
            throw std::invalid_argument("RBTrimBits width > 256");

        if (width == 256)
        {
            return;
        }

        const size_t full = width / 64;
        const size_t rem = width % 64;
        for (size_t i = full + (rem ? 1 : 0); i < bits.size(); ++i)
        {
            bits[i] = 0;
        }
        if (rem != 0 && full < bits.size())
        {
            bits[full] &= ((1ULL << rem) - 1ULL);
        }
    }

    inline void RBMaskBits(const std::array<u64, 4>& m, size_t width, RBBits& bits)
    {
        bits = m;
        RBTrimBits(bits, width);
    }

    inline bool RBHasBits(const RBBits& bits)
    {
        return (bits[0] | bits[1] | bits[2] | bits[3]) != 0;
    }

    inline void RBEnsureBits(RBBits& bits)
    {
        if (!RBHasBits(bits))
        {
            bits[0] = 1;
        }
    }

    inline bool RBBit(const RBBits& bits, size_t idx)
    {
        return ((bits[idx / 64] >> (idx % 64)) & 1ULL) != 0;
    }

    inline size_t RBLead(const RBBits& bits, size_t width)
    {
        for (size_t i = 0; i < bits.size(); ++i)
        {
            if (bits[i] != 0)
            {
                return i * 64 + static_cast<size_t>(__builtin_ctzll(bits[i]));
            }
        }
        return width;
    }

    inline void RBZeroBelow(RBBits& bits, size_t idx)
    {
        const size_t limb = idx / 64;
        const size_t off = idx % 64;
        for (size_t i = 0; i < limb; ++i)
        {
            bits[i] = 0;
        }
        if (limb < bits.size() && off != 0)
        {
            bits[limb] &= ~((1ULL << off) - 1ULL);
        }
    }

    inline RBBits RBShiftLeft(const RBBits& in, size_t shift)
    {
        if (shift >= 256)
            return {};

        RBBits out{};
        const size_t limbShift = shift / 64;
        const size_t bitShift = shift % 64;
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == 0)
                continue;
            const size_t dst = i + limbShift;
            if (dst < out.size())
            {
                out[dst] |= in[i] << bitShift;
            }
            if (bitShift != 0 && dst + 1 < out.size())
            {
                out[dst + 1] |= in[i] >> (64 - bitShift);
            }
        }
        return out;
    }

    inline RBBits RBShiftRight(const RBBits& in, size_t shift)
    {
        if (shift >= 256)
            return {};

        RBBits out{};
        const size_t limbShift = shift / 64;
        const size_t bitShift = shift % 64;
        for (size_t i = limbShift; i < in.size(); ++i)
        {
            const size_t dst = i - limbShift;
            out[dst] |= in[i] >> bitShift;
            if (bitShift != 0 && i + 1 < in.size())
            {
                out[dst] |= in[i + 1] << (64 - bitShift);
            }
        }
        return out;
    }

    inline void RBXorShifted(RBBits& dst, size_t dstOff, const RBBits& src, size_t srcOff, size_t width)
    {
        RBBits tmp = src;
        RBZeroBelow(tmp, srcOff);

        const ptrdiff_t shift = static_cast<ptrdiff_t>(dstOff) - static_cast<ptrdiff_t>(srcOff);
        tmp = (shift >= 0) ? RBShiftLeft(tmp, static_cast<size_t>(shift))
                           : RBShiftRight(tmp, static_cast<size_t>(-shift));
        RBTrimBits(tmp, width);
        for (size_t i = 0; i < dst.size(); ++i)
        {
            dst[i] ^= tmp[i];
        }
    }

    inline void RBXor(std::vector<block>& a, const std::vector<block>& b)
    {
        if (a.size() != b.size())
            throw std::invalid_argument("RBXor size mismatch");

        for (size_t i = 0; i < a.size(); ++i)
            a[i] ^= b[i];
    }

    inline void RBXor(std::vector<block>& a, const block* b, size_t n)
    {
        if (a.size() != n)
            throw std::invalid_argument("RBXor ptr size mismatch");

        for (size_t i = 0; i < n; ++i)
            a[i] ^= b[i];
    }

    inline void RBXor(block* a, const block* b, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            a[i] ^= b[i];
    }

    inline bool RBRows(
        const std::vector<block>& setKeys,
        const std::vector<std::vector<block>>& setValues,
        size_t columns,
        size_t bandWidth,
        const RBParams& params,
        std::vector<RBRow>& rows)
    {
        if (setKeys.size() != setValues.size())
            throw std::invalid_argument("RBRows key/value mismatch");
        if (setKeys.empty())
        {
            rows.clear();
            return true;
        }

        const size_t rowSize = setValues[0].size();
        if (rowSize == 0)
            throw std::invalid_argument("RBRows empty row");
        if (columns < bandWidth)
            throw std::invalid_argument("RBRows columns < bandWidth");

        rows.resize(setKeys.size());
        RBFor(setKeys.size(), params.multiThread, params.workerThreads, [&](size_t begin, size_t end)
        {
            const auto hash = RBMakeHashCtx(params);
            for (size_t i = begin; i < end; ++i)
            {
                if (setValues[i].size() != rowSize)
                    throw std::invalid_argument("RBRows row width mismatch");

                const u64 h = RBHash64(setKeys[i], hash.start);
                const size_t range = columns - bandWidth + 1;
                rows[i].start = (range == 0) ? 0 : static_cast<size_t>(h % range);
                rows[i].rhs = setValues[i];
                RBMaskBits(RBMask256(setKeys[i], hash), bandWidth, rows[i].bits);
                RBEnsureBits(rows[i].bits);
            }
        });

        std::stable_sort(rows.begin(), rows.end(), [](const RBRow& a, const RBRow& b)
        {
            return a.start < b.start;
        });
        return true;
    }

    inline block* RBFlatPtr(std::vector<block>& values, size_t rowSize, const RBFlatRow& row)
    {
        return values.data() + row.rhs;
    }

    inline const block* RBFlatPtr(const std::vector<block>& values, size_t rowSize, const RBFlatRow& row)
    {
        return values.data() + row.rhs;
    }

    inline bool RBRowsFlat(
        const std::vector<block>& setKeys,
        const std::vector<block>& setValues,
        size_t rowSize,
        size_t columns,
        size_t bandWidth,
        const RBParams& params,
        std::vector<RBFlatRow>& rows,
        std::vector<block>& rhs)
    {
        if (rowSize == 0)
            throw std::invalid_argument("RBRowsFlat empty row");
        if (setValues.size() != setKeys.size() * rowSize)
            throw std::invalid_argument("RBRowsFlat key/value mismatch");
        if (setKeys.empty())
        {
            rows.clear();
            rhs.clear();
            return true;
        }
        if (columns < bandWidth)
            throw std::invalid_argument("RBRowsFlat columns < bandWidth");

        rows.resize(setKeys.size());
        rhs.resize(setValues.size());
        RBFor(setKeys.size(), params.multiThread, params.workerThreads, [&](size_t begin, size_t end)
        {
            const auto hash = RBMakeHashCtx(params);
            for (size_t i = begin; i < end; ++i)
            {
                const u64 h = RBHash64(setKeys[i], hash.start);
                const size_t range = columns - bandWidth + 1;
                rows[i].start = (range == 0) ? 0 : static_cast<size_t>(h % range);
                rows[i].rhs = i * rowSize;
                std::memcpy(rhs.data() + rows[i].rhs, setValues.data() + rows[i].rhs, rowSize * sizeof(block));
                RBMaskBits(RBMask256(setKeys[i], hash), bandWidth, rows[i].bits);
                RBEnsureBits(rows[i].bits);
            }
        });

        std::stable_sort(rows.begin(), rows.end(), [](const RBFlatRow& a, const RBFlatRow& b)
        {
            return a.start < b.start;
        });
        return true;
    }
}
