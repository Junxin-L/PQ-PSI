#include "okvs/rbokvs.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace osuCrypto;

namespace
{
    struct Args
    {
        std::string out;
        size_t n = 128;
        double eps = 0.10;
        size_t w = 64;
        size_t rounds = 10;
        u64 seedBase = 1;
    };

    struct Counts
    {
        size_t pass = 0;
        size_t fail = 0;
        size_t mismatch = 0;
    };

    struct Series
    {
        std::vector<double> runMs;
    };

    double meanOf(const std::vector<double>& xs)
    {
        if (xs.empty())
        {
            return 0.0;
        }
        return std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
    }

    Args parseArgs(int argc, char** argv)
    {
        if (argc < 6)
        {
            throw std::invalid_argument(
                "usage: rbokvs_g_check <out.md> <setSize> <eps> <w> <rounds> [seedBase]");
        }

        Args args;
        args.out = argv[1];
        args.n = static_cast<size_t>(std::stoull(argv[2]));
        args.eps = std::stod(argv[3]);
        args.w = static_cast<size_t>(std::stoull(argv[4]));
        args.rounds = static_cast<size_t>(std::stoull(argv[5]));
        if (argc >= 7)
        {
            args.seedBase = static_cast<u64>(std::stoull(argv[6]));
        }
        return args;
    }

    bool runOne(const Args& args, size_t roundIdx, double& runMs)
    {
        PRNG prng(toBlock(0x72626f6b7673ULL, args.seedBase + roundIdx + 1));

        std::vector<block> keys(args.n);
        std::vector<block> vals(args.n);
        for (size_t i = 0; i < args.n; ++i)
        {
            keys[i] = prng.get<block>() ^ toBlock(static_cast<u64>(roundIdx + 1), static_cast<u64>(i + 1));
            vals[i] = prng.get<block>() ^ toBlock(0xfeed0000ULL + static_cast<u64>(roundIdx), static_cast<u64>(i));
        }

        RBParams rb;
        rb.lambda = 1;
        rb.eps = args.eps;
        rb.bandWidth = args.w;
        rb.columns = static_cast<size_t>(
            std::ceil((1.0 + args.eps) * static_cast<double>(std::max<size_t>(args.n, 1))));

        std::vector<block> table;
        const auto t0 = std::chrono::steady_clock::now();
        const bool encOk = RBEncode(keys, vals, table, rb);
        std::vector<block> got;
        if (encOk)
        {
            RBDecode(table, keys, got, rb);
        }
        const auto t1 = std::chrono::steady_clock::now();
        runMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!encOk)
        {
            return false;
        }
        if (got.size() != vals.size())
        {
            return false;
        }
        for (size_t i = 0; i < vals.size(); ++i)
        {
            if (std::memcmp(&got[i], &vals[i], sizeof(block)) != 0)
            {
                return false;
            }
        }
        return true;
    }

    void writeReport(const Args& args, const Counts& cnt, const Series& ser)
    {
        std::ofstream out(args.out, std::ios::out | std::ios::trunc);
        if (!out)
        {
            throw std::runtime_error("failed to open output file: " + args.out);
        }

        const size_t m = static_cast<size_t>(
            std::ceil((1.0 + args.eps) * static_cast<double>(std::max<size_t>(args.n, 1))));
        const double g40 = 40.0 - 2.751 * args.eps * static_cast<double>(args.w);

        out << "# RB-OKVS G Check\n\n";
        out << std::fixed << std::setprecision(2);
        out << "## Summary\n";
        out << "| item | value |\n";
        out << "|---|---:|\n";
        out << "| set_size | " << args.n << " |\n";
        out << "| rounds | " << args.rounds << " |\n";
        out << "| rb_lambda_check | 1 |\n";
        out << "| rb_eps | " << args.eps << " |\n";
        out << "| rb_m | " << m << " |\n";
        out << "| rb_w | " << args.w << " |\n";
        out << "| g_40_from_w | " << g40 << " |\n";
        out << "| okvs_runtime_mean_ms | " << meanOf(ser.runMs) << " |\n";
        out << "| okvs_pass_rounds | " << cnt.pass << " |\n";
        out << "| okvs_fail_rounds | " << cnt.fail << " |\n";
        out << "| overall_status | " << (cnt.fail == 0 ? "ok" : "needs_check") << " |\n\n";

        out << "## Per-Round Results\n";
        out << "| round | okvs_runtime_ms |\n";
        out << "|---:|---:|\n";
        for (size_t i = 0; i < ser.runMs.size(); ++i)
        {
            out << "| " << (i + 1) << " | " << ser.runMs[i] << " |\n";
        }
        out << "\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        const Args args = parseArgs(argc, argv);
        Counts cnt;
        Series ser;
        ser.runMs.reserve(args.rounds);

        for (size_t i = 0; i < args.rounds; ++i)
        {
            double runMs = 0.0;
            const bool ok = runOne(args, i, runMs);
            ser.runMs.push_back(runMs);
            if (ok)
            {
                ++cnt.pass;
            }
            else
            {
                ++cnt.fail;
                ++cnt.mismatch;
            }
        }

        writeReport(args, cnt, ser);
        return cnt.fail == 0 ? 0 : 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "rbokvs_g_check failed: " << e.what() << "\n";
        return 1;
    }
}
