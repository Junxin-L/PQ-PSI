#pragma once

#include "rows.h"

namespace osuCrypto
{
    inline block* RBRowPtr(std::vector<block>& table, size_t rowSize, size_t row)
    {
        return table.data() + row * rowSize;
    }

    inline const block* RBRowPtr(const std::vector<block>& table, size_t rowSize, size_t row)
    {
        return table.data() + row * rowSize;
    }

    inline bool RBEncode(
        const std::vector<block>& setKeys,
        const std::vector<std::vector<block>>& setValues,
        std::vector<std::vector<block>>& okvsTable,
        RBParams params = {})
    {
        if (setKeys.empty())
        {
            okvsTable.clear();
            return true;
        }

        const auto cfg = RBInfoOf(setKeys.size(), params);
        const size_t columns = cfg.columns;
        const size_t bandWidth = cfg.bandWidth;

        std::vector<RBRow> rows;
        RBRows(setKeys, setValues, columns, bandWidth, params, rows);

        const size_t rowCnt = rows.size();
        const size_t valWidth = setValues[0].size();
        std::vector<size_t> pivot(rowCnt, columns);

        // forward eliminate
        for (size_t i = 0; i < rowCnt; ++i)
        {
            const size_t lead = RBLead(rows[i].bits, bandWidth);
            if (lead == bandWidth)
                return false;

            pivot[i] = rows[i].start + lead;

            for (size_t j = i + 1; j < rowCnt; ++j)
            {
                if (rows[j].start > pivot[i])
                    break;

                const size_t off = pivot[i] - rows[j].start;
                if (off >= bandWidth || !RBBit(rows[j].bits, off))
                    continue;

                RBXorShifted(rows[j].bits, off, rows[i].bits, lead, bandWidth);
                RBXor(rows[j].rhs, rows[i].rhs);
            }
        }

        // back fill table
        okvsTable.assign(columns, std::vector<block>(valWidth, ZeroBlock));
        for (ptrdiff_t i = static_cast<ptrdiff_t>(rowCnt) - 1; i >= 0; --i)
        {
            std::vector<block> sum = rows[static_cast<size_t>(i)].rhs;
            const size_t s = rows[static_cast<size_t>(i)].start;
            const size_t p = pivot[static_cast<size_t>(i)];
            for (size_t j = 0; j < bandWidth; ++j)
            {
                if (!RBBit(rows[static_cast<size_t>(i)].bits, j))
                    continue;
                const size_t col = s + j;
                if (col == p)
                    continue;
                RBXor(sum, okvsTable[col]);
            }
            okvsTable[p] = std::move(sum);
        }
        return true;
    }

    inline bool RBEncode(
        const std::vector<block>& setKeys,
        const std::vector<std::vector<block>>& setValues,
        std::vector<block>& okvsTable,
        size_t rowSize,
        RBParams params = {})
    {
        if (setKeys.empty())
        {
            okvsTable.clear();
            return true;
        }
        if (setValues.empty() || setValues[0].size() != rowSize)
            throw std::invalid_argument("RBEncode flat row width mismatch");

        const auto cfg = RBInfoOf(setKeys.size(), params);
        const size_t columns = cfg.columns;
        const size_t bandWidth = cfg.bandWidth;

        std::vector<RBRow> rows;
        RBRows(setKeys, setValues, columns, bandWidth, params, rows);

        const size_t rowCnt = rows.size();
        std::vector<size_t> pivot(rowCnt, columns);

        for (size_t i = 0; i < rowCnt; ++i)
        {
            const size_t lead = RBLead(rows[i].bits, bandWidth);
            if (lead == bandWidth)
                return false;

            pivot[i] = rows[i].start + lead;

            for (size_t j = i + 1; j < rowCnt; ++j)
            {
                if (rows[j].start > pivot[i])
                    break;

                const size_t off = pivot[i] - rows[j].start;
                if (off >= bandWidth || !RBBit(rows[j].bits, off))
                    continue;

                RBXorShifted(rows[j].bits, off, rows[i].bits, lead, bandWidth);
                RBXor(rows[j].rhs, rows[i].rhs);
            }
        }

        okvsTable.assign(columns * rowSize, ZeroBlock);
        for (ptrdiff_t i = static_cast<ptrdiff_t>(rowCnt) - 1; i >= 0; --i)
        {
            std::vector<block> sum = rows[static_cast<size_t>(i)].rhs;
            const size_t s = rows[static_cast<size_t>(i)].start;
            const size_t p = pivot[static_cast<size_t>(i)];
            for (size_t j = 0; j < bandWidth; ++j)
            {
                if (!RBBit(rows[static_cast<size_t>(i)].bits, j))
                    continue;
                const size_t col = s + j;
                if (col == p)
                    continue;
                RBXor(sum, RBRowPtr(okvsTable, rowSize, col), rowSize);
            }
            std::memcpy(RBRowPtr(okvsTable, rowSize, p), sum.data(), rowSize * sizeof(block));
        }
        return true;
    }

    inline void RBDecode(
        const std::vector<std::vector<block>>& okvsTable,
        const std::vector<block>& setKeys,
        std::vector<std::vector<block>>& setValues,
        RBParams params = {})
    {
        if (okvsTable.empty())
        {
            setValues.clear();
            return;
        }

        const size_t columns = okvsTable.size();
        const size_t valWidth = okvsTable[0].size();
        if (params.columns != 0 && params.columns != columns)
            throw std::invalid_argument("RBDecode columns mismatch");

        const size_t bandWidth = RBTableW(setKeys.size(), columns, params);
        if (bandWidth > 256)
            throw std::invalid_argument("RBDecode width > 256");
        if (bandWidth > columns)
            throw std::invalid_argument("RBDecode width > columns");

        setValues.assign(setKeys.size(), std::vector<block>(valWidth, ZeroBlock));
        RBFor(setKeys.size(), params.multiThread, [&](size_t begin, size_t end)
        {
            const auto hash = RBMakeHashCtx(params);
            for (size_t i = begin; i < end; ++i)
            {
                const u64 h = RBHash64(setKeys[i], hash.start);
                const size_t range = columns - bandWidth + 1;
                const size_t start = (range == 0) ? 0 : static_cast<size_t>(h % range);

                RBBits bits{};
                RBMaskBits(RBMask256(setKeys[i], hash), bandWidth, bits);
                RBEnsureBits(bits);

                for (size_t j = 0; j < bandWidth; ++j)
                {
                    if (!RBBit(bits, j))
                        continue;
                    const size_t col = start + j;
                    if (col >= columns)
                        break;
                    for (size_t k = 0; k < valWidth; ++k)
                        setValues[i][k] ^= okvsTable[col][k];
                }
            }
        });
    }

    inline void RBDecode(
        const std::vector<block>& okvsTable,
        size_t rowSize,
        const std::vector<block>& setKeys,
        std::vector<std::vector<block>>& setValues,
        RBParams params = {})
    {
        if (okvsTable.empty())
        {
            setValues.clear();
            return;
        }
        if (rowSize == 0 || (okvsTable.size() % rowSize) != 0)
            throw std::invalid_argument("RBDecode flat table size mismatch");

        const size_t columns = okvsTable.size() / rowSize;
        if (params.columns != 0 && params.columns != columns)
            throw std::invalid_argument("RBDecode columns mismatch");

        const size_t bandWidth = RBTableW(setKeys.size(), columns, params);
        if (bandWidth > 256)
            throw std::invalid_argument("RBDecode width > 256");
        if (bandWidth > columns)
            throw std::invalid_argument("RBDecode width > columns");

        setValues.assign(setKeys.size(), std::vector<block>(rowSize, ZeroBlock));
        RBFor(setKeys.size(), params.multiThread, [&](size_t begin, size_t end)
        {
            const auto hash = RBMakeHashCtx(params);
            for (size_t i = begin; i < end; ++i)
            {
                const u64 h = RBHash64(setKeys[i], hash.start);
                const size_t range = columns - bandWidth + 1;
                const size_t start = (range == 0) ? 0 : static_cast<size_t>(h % range);

                RBBits bits{};
                RBMaskBits(RBMask256(setKeys[i], hash), bandWidth, bits);
                RBEnsureBits(bits);

                for (size_t j = 0; j < bandWidth; ++j)
                {
                    if (!RBBit(bits, j))
                        continue;
                    const size_t col = start + j;
                    if (col >= columns)
                        break;
                    RBXor(setValues[i], RBRowPtr(okvsTable, rowSize, col), rowSize);
                }
            }
        });
    }

    inline bool RBEncode(
        const std::vector<block>& setKeys,
        const std::vector<block>& setValues,
        std::vector<block>& okvsTable,
        RBParams params = {})
    {
        if (setKeys.size() != setValues.size())
            throw std::invalid_argument("RBEncode 1D mismatch");

        std::vector<std::vector<block>> in(setValues.size(), std::vector<block>(1));
        for (size_t i = 0; i < setValues.size(); ++i)
            in[i][0] = setValues[i];

        std::vector<std::vector<block>> out2d;
        const bool ok = RBEncode(setKeys, in, out2d, params);
        if (!ok)
            return false;

        okvsTable.resize(out2d.size());
        for (size_t i = 0; i < out2d.size(); ++i)
            okvsTable[i] = out2d[i][0];
        return true;
    }

    inline void RBDecode(
        const std::vector<block>& okvsTable,
        const std::vector<block>& setKeys,
        std::vector<block>& setValues,
        RBParams params = {})
    {
        std::vector<std::vector<block>> table2d(okvsTable.size(), std::vector<block>(1));
        for (size_t i = 0; i < okvsTable.size(); ++i)
            table2d[i][0] = okvsTable[i];

        std::vector<std::vector<block>> out2d;
        RBDecode(table2d, setKeys, out2d, params);
        setValues.resize(out2d.size());
        for (size_t i = 0; i < out2d.size(); ++i)
            setValues[i] = out2d[i][0];
    }

    using RbOkvsParams = RBParams;
    using RbRow = RBRow;
    using RBResolved = RBInfo;

    inline size_t RbOkvsDefaultBandWidth(size_t n) { return RBWidth(n); }
    inline size_t RbOkvsTableSize(size_t n, size_t lambda = 40) { return RBSize(n, lambda); }
    inline double RbOkvsEps(size_t n, size_t columns) { return RBEpsOf(n, columns); }
    inline size_t RbOkvsCols(size_t n, RbOkvsParams params = {}) { return RBTableSize(n, params); }
    inline size_t RbOkvsNeedBandWidth(size_t n, size_t lambda, double eps) { return RBNeedW(n, lambda, eps); }
    inline size_t RbOkvsBandWidthForTable(size_t n, size_t columns, RbOkvsParams params = {}) { return RBTableW(n, columns, params); }
    inline RBInfo RbOkvsResolve(size_t n, RbOkvsParams params = {}) { return RBInfoOf(n, params); }
    inline double RbOkvsLambda(size_t n, size_t bandWidth, double eps) { return RBLambdaOf(n, bandWidth, eps); }

    inline double RBEps(size_t n, size_t columns) { return RBEpsOf(n, columns); }
    inline size_t RBCols(size_t n, RBParams params = {}) { return RBTableSize(n, params); }
    inline size_t RBNeedWidth(size_t n, size_t lambda, double eps) { return RBNeedW(n, lambda, eps); }
    inline size_t RBWidthForTable(size_t n, size_t columns, RBParams params = {}) { return RBTableW(n, columns, params); }
    inline RBInfo RBPick(size_t n, RBParams params = {}) { return RBInfoOf(n, params); }
    inline double RBLambda(size_t n, size_t bandWidth, double eps) { return RBLambdaOf(n, bandWidth, eps); }
    inline u64 RbHash64(block key, block seed) { return RBHash64(key, seed); }
    inline std::array<u64, 4> RbMask256(block key, block seed) { return RBMask256(key, seed); }
    inline void RbMaskToBits(const std::array<u64, 4>& m, size_t width, RBBits& bits) { RBMaskBits(m, width, bits); }
    inline void RbXorVec(std::vector<block>& a, const std::vector<block>& b) { RBXor(a, b); }
    inline bool RbPrepareRows(
        const std::vector<block>& setKeys,
        const std::vector<std::vector<block>>& setValues,
        size_t columns,
        size_t bandWidth,
        const RbOkvsParams& params,
        std::vector<RbRow>& rows)
    {
        return RBRows(setKeys, setValues, columns, bandWidth, params, rows);
    }

    inline bool RandomBandOkvsEncode(
        const std::vector<block>& setKeys,
        const std::vector<std::vector<block>>& setValues,
        std::vector<std::vector<block>>& okvsTable,
        RbOkvsParams params = {})
    {
        return RBEncode(setKeys, setValues, okvsTable, params);
    }

    inline void RandomBandOkvsDecode(
        const std::vector<std::vector<block>>& okvsTable,
        const std::vector<block>& setKeys,
        std::vector<std::vector<block>>& setValues,
        RbOkvsParams params = {})
    {
        RBDecode(okvsTable, setKeys, setValues, params);
    }

    inline bool RandomBandOkvsEncode(
        const std::vector<block>& setKeys,
        const std::vector<block>& setValues,
        std::vector<block>& okvsTable,
        RbOkvsParams params = {})
    {
        return RBEncode(setKeys, setValues, okvsTable, params);
    }

    inline void RandomBandOkvsDecode(
        const std::vector<block>& okvsTable,
        const std::vector<block>& setKeys,
        std::vector<block>& setValues,
        RbOkvsParams params = {})
    {
        RBDecode(okvsTable, setKeys, setValues, params);
    }
}
