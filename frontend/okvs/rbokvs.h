#pragma once

#include "Crypto/PRNG.h"
#include "Common/Defines.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace osuCrypto
{
    inline size_t RbOkvsDefaultBandWidth(size_t n)
    {
        size_t bw = n / 64;
        if (bw < 16) bw = 16;
        if (bw > 256) bw = 256;
        return bw;
    }

    inline size_t RbOkvsTableSize(size_t n, size_t lambda = 40)
    {
        n = std::max<size_t>(n, 1);
        const size_t w = RbOkvsDefaultBandWidth(n);
        const double eps = static_cast<double>(lambda) / (2.751 * static_cast<double>(w));
        return std::max<size_t>(n, static_cast<size_t>(std::ceil((1.0 + eps) * static_cast<double>(n))));
    }

    struct RbOkvsParams
    {
        size_t lambda = 40;
        size_t bandWidth = 0;
        block startSeed = toBlock(0x9b3a7f21ULL, 0x6c8e1d44ULL);
        block maskSeed = toBlock(0x4f12c0deULL, 0xa55a91b7ULL);
    };

    inline u64 RbHash64(block key, block seed)
    {
        AES h;
        h.setKey(seed);
        block x = h.ecbEncBlock(key);
        const u64* w = reinterpret_cast<const u64*>(&x);
        return w[0] ^ w[1];
    }

    inline std::array<u64, 4> RbMask256(block key, block seed)
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

    inline void RbMaskToBits(const std::array<u64, 4>& m, size_t width, std::vector<u8>& bits)
    {
        bits.assign(width, 0);
        for (size_t limb = 0; limb < 4; ++limb)
        {
            for (size_t k = 0; k < 64; ++k)
            {
                const size_t idx = limb * 64 + k;
                if (idx >= width) return;
                bits[idx] = static_cast<u8>((m[limb] >> k) & 1ULL);
            }
        }
    }

    inline void RbXorVec(std::vector<block>& a, const std::vector<block>& b)
    {
        if (a.size() != b.size()) throw std::invalid_argument("RbXorVec size mismatch");
        for (size_t i = 0; i < a.size(); ++i) a[i] ^= b[i];
    }

    struct RbRow
    {
        size_t start = 0;
        std::vector<u8> bits;
        std::vector<block> rhs;
    };

    inline bool RbPrepareRows(
        const std::vector<block>& setKeys,
        const std::vector<std::vector<block>>& setValues,
        size_t columns,
        size_t bandWidth,
        const RbOkvsParams& params,
        std::vector<RbRow>& rows)
    {
        if (setKeys.size() != setValues.size()) throw std::invalid_argument("RbPrepareRows key/value mismatch");
        if (setKeys.empty())
        {
            rows.clear();
            return true;
        }

        const size_t rowSize = setValues[0].size();
        if (rowSize == 0) throw std::invalid_argument("RbPrepareRows empty row");
        if (columns < bandWidth) throw std::invalid_argument("RbPrepareRows columns < bandWidth");

        rows.resize(setKeys.size());
        for (size_t i = 0; i < setKeys.size(); ++i)
        {
            if (setValues[i].size() != rowSize) throw std::invalid_argument("RbPrepareRows row width mismatch");

            const u64 h = RbHash64(setKeys[i], params.startSeed);
            const size_t range = columns - bandWidth + 1;
            rows[i].start = (range == 0) ? 0 : static_cast<size_t>(h % range);
            rows[i].rhs = setValues[i];
            RbMaskToBits(RbMask256(setKeys[i], params.maskSeed), bandWidth, rows[i].bits);

            bool allZero = true;
            for (auto b : rows[i].bits) if (b) { allZero = false; break; }
            if (allZero && !rows[i].bits.empty()) rows[i].bits[0] = 1;
        }

        std::stable_sort(rows.begin(), rows.end(), [](const RbRow& a, const RbRow& b) { return a.start < b.start; });
        return true;
    }

    inline bool RandomBandOkvsEncode(
        const std::vector<block>& setKeys,
        const std::vector<std::vector<block>>& setValues,
        std::vector<std::vector<block>>& okvsTable,
        RbOkvsParams params = {})
    {
        if (setKeys.empty())
        {
            okvsTable.clear();
            return true;
        }

        const size_t columns = RbOkvsTableSize(setKeys.size(), params.lambda);
        size_t bandWidth = (params.bandWidth == 0) ? RbOkvsDefaultBandWidth(columns) : params.bandWidth;
        if (bandWidth == 0) bandWidth = 1;
        if (bandWidth > columns) bandWidth = columns;

        std::vector<RbRow> rows;
        RbPrepareRows(setKeys, setValues, columns, bandWidth, params, rows);

        const size_t rowCnt = rows.size();
        const size_t valWidth = setValues[0].size();
        std::vector<size_t> pivot(rowCnt, columns);

        for (size_t i = 0; i < rowCnt; ++i)
        {
            size_t lead = bandWidth;
            for (size_t j = 0; j < bandWidth; ++j)
            {
                if (rows[i].bits[j]) { lead = j; break; }
            }
            if (lead == bandWidth) return false;
            pivot[i] = rows[i].start + lead;

            for (size_t j = i + 1; j < rowCnt; ++j)
            {
                if (rows[j].start > pivot[i]) break;
                const size_t off = pivot[i] - rows[j].start;
                if (off >= bandWidth || rows[j].bits[off] == 0) continue;

                for (size_t k = 0; k + lead < bandWidth && k + off < bandWidth; ++k)
                    rows[j].bits[k + off] ^= rows[i].bits[k + lead];
                RbXorVec(rows[j].rhs, rows[i].rhs);
            }
        }

        okvsTable.assign(columns, std::vector<block>(valWidth, ZeroBlock));
        for (ptrdiff_t i = static_cast<ptrdiff_t>(rowCnt) - 1; i >= 0; --i)
        {
            std::vector<block> sum = rows[static_cast<size_t>(i)].rhs;
            const size_t s = rows[static_cast<size_t>(i)].start;
            const size_t p = pivot[static_cast<size_t>(i)];
            for (size_t j = 0; j < bandWidth; ++j)
            {
                if (!rows[static_cast<size_t>(i)].bits[j]) continue;
                const size_t col = s + j;
                if (col == p) continue;
                RbXorVec(sum, okvsTable[col]);
            }
            okvsTable[p] = std::move(sum);
        }
        return true;
    }

    inline void RandomBandOkvsDecode(
        const std::vector<std::vector<block>>& okvsTable,
        const std::vector<block>& setKeys,
        std::vector<std::vector<block>>& setValues,
        RbOkvsParams params = {})
    {
        if (okvsTable.empty())
        {
            setValues.clear();
            return;
        }
        const size_t columns = okvsTable.size();
        const size_t valWidth = okvsTable[0].size();
        size_t bandWidth = (params.bandWidth == 0) ? RbOkvsDefaultBandWidth(columns) : params.bandWidth;
        if (bandWidth == 0) bandWidth = 1;
        if (bandWidth > columns) bandWidth = columns;

        setValues.assign(setKeys.size(), std::vector<block>(valWidth, ZeroBlock));
        for (size_t i = 0; i < setKeys.size(); ++i)
        {
            const u64 h = RbHash64(setKeys[i], params.startSeed);
            const size_t range = columns - bandWidth + 1;
            const size_t start = (range == 0) ? 0 : static_cast<size_t>(h % range);

            std::vector<u8> bits;
            RbMaskToBits(RbMask256(setKeys[i], params.maskSeed), bandWidth, bits);
            bool allZero = true;
            for (auto b : bits) if (b) { allZero = false; break; }
            if (allZero && !bits.empty()) bits[0] = 1;

            for (size_t j = 0; j < bandWidth; ++j)
            {
                if (!bits[j]) continue;
                const size_t col = start + j;
                if (col >= columns) break;
                for (size_t k = 0; k < valWidth; ++k) setValues[i][k] ^= okvsTable[col][k];
            }
        }
    }

    inline bool RandomBandOkvsEncode(
        const std::vector<block>& setKeys,
        const std::vector<block>& setValues,
        std::vector<block>& okvsTable,
        RbOkvsParams params = {})
    {
        if (setKeys.size() != setValues.size()) throw std::invalid_argument("RandomBandOkvsEncode 1D mismatch");
        std::vector<std::vector<block>> in(setValues.size(), std::vector<block>(1));
        for (size_t i = 0; i < setValues.size(); ++i) in[i][0] = setValues[i];

        std::vector<std::vector<block>> out2d;
        const bool ok = RandomBandOkvsEncode(setKeys, in, out2d, params);
        if (!ok) return false;

        okvsTable.resize(out2d.size());
        for (size_t i = 0; i < out2d.size(); ++i) okvsTable[i] = out2d[i][0];
        return true;
    }

    inline void RandomBandOkvsDecode(
        const std::vector<block>& okvsTable,
        const std::vector<block>& setKeys,
        std::vector<block>& setValues,
        RbOkvsParams params = {})
    {
        std::vector<std::vector<block>> table2d(okvsTable.size(), std::vector<block>(1));
        for (size_t i = 0; i < okvsTable.size(); ++i) table2d[i][0] = okvsTable[i];
        std::vector<std::vector<block>> out2d;
        RandomBandOkvsDecode(table2d, setKeys, out2d, params);
        setValues.resize(out2d.size());
        for (size_t i = 0; i < out2d.size(); ++i) setValues[i] = out2d[i][0];
    }
}
