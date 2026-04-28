#include "okvs/rbokvs.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
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

    struct GridCase
    {
        size_t n = 0;
        double eps = 0.0;
        size_t w = 0;
        size_t m = 0;
        size_t pass = 0;
        size_t fail = 0;
        double runtimeMs = 0.0;
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
        rb.lambda = 40;
        rb.check = false;
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

    bool runOneRaw(size_t n, double eps, size_t w, u64 seedBase, size_t roundIdx, double& runMs)
    {
        Args args;
        args.n = n;
        args.eps = eps;
        args.w = w;
        args.rounds = 1;
        args.seedBase = seedBase;
        return runOne(args, roundIdx, runMs);
    }

    std::vector<size_t> defaultSizes()
    {
        return {128, 256, 512, 1024, 2048};
    }

    std::vector<double> defaultEps(size_t n)
    {
        std::vector<double> out;
        const int lo = (n <= 128) ? 10 : 5;
        const int hi = (n <= 128) ? 24 : 14;
        for (int e = lo; e <= hi; ++e)
        {
            out.push_back(static_cast<double>(e) / 100.0);
        }
        return out;
    }

    std::vector<size_t> defaultW()
    {
        std::vector<size_t> out;
        for (size_t w = 16; w <= 256; w += 8)
        {
            out.push_back(w);
        }
        return out;
    }

    std::vector<size_t> rangeW(size_t lo, size_t hi, size_t step)
    {
        std::vector<size_t> out;
        if (step == 0)
        {
            step = 1;
        }
        for (size_t w = lo; w <= hi; w += step)
        {
            out.push_back(w);
            if (hi - w < step)
            {
                break;
            }
        }
        return out;
    }

    std::vector<size_t> parseSizes(const char* text)
    {
        if (!text || !*text)
        {
            return defaultSizes();
        }

        std::vector<size_t> out;
        std::istringstream in(text);
        size_t v = 0;
        while (in >> v)
        {
            out.push_back(v);
        }
        return out.empty() ? defaultSizes() : out;
    }

    std::vector<double> parseDoubles(const char* text)
    {
        std::vector<double> out;
        if (!text || !*text)
        {
            return out;
        }

        std::istringstream in(text);
        double v = 0.0;
        while (in >> v)
        {
            out.push_back(v);
        }
        return out;
    }

    std::vector<size_t> parseWs(const char* text)
    {
        if (!text || !*text)
        {
            return defaultW();
        }

        std::vector<size_t> out;
        std::istringstream in(text);
        size_t v = 0;
        while (in >> v)
        {
            out.push_back(v);
        }
        return out.empty() ? defaultW() : out;
    }

    std::string sizeLabel(size_t n)
    {
        size_t p = 0;
        size_t v = n;
        while (v > 1 && (v % 2) == 0)
        {
            v /= 2;
            ++p;
        }
        if (v == 1)
        {
            return "2^" + std::to_string(p);
        }
        return std::to_string(n);
    }

    GridCase runGridCase(size_t n, double eps, size_t w, size_t rounds, u64 seedBase)
    {
        GridCase c;
        c.n = n;
        c.eps = eps;
        c.w = w;
        c.m = static_cast<size_t>(
            std::ceil((1.0 + eps) * static_cast<double>(std::max<size_t>(n, 1))));
        if (w > c.m)
        {
            c.fail = rounds;
            return c;
        }

        double sumMs = 0.0;
        for (size_t r = 0; r < rounds; ++r)
        {
            double runMs = 0.0;
            const bool ok = runOneRaw(n, eps, w, seedBase + n * 100000 + w * 1000, r, runMs);
            sumMs += runMs;
            if (ok)
            {
                ++c.pass;
            }
            else
            {
                ++c.fail;
            }
        }
        c.runtimeMs = rounds == 0 ? 0.0 : sumMs / static_cast<double>(rounds);
        return c;
    }

    void writeGridReport(
        const std::string& outPath,
        const std::vector<GridCase>& cases,
        size_t rounds,
        u64 seedBase,
        double lambdaTarget)
    {
        std::ofstream out(outPath, std::ios::out | std::ios::trunc);
        if (!out)
        {
            throw std::runtime_error("failed to open output file: " + outPath);
        }

        out << "# RB-OKVS Parameter Grid\n\n";
        out << std::fixed << std::setprecision(4);
        out << "`rounds_per_case=" << rounds << "` "
            << "`seed_base=" << seedBase << "` "
            << "`lambda_target=" << lambdaTarget << "`\n\n";

        out << "## Best W By Size And Eps\n\n";
        out << "| Size | eps | w_min_pass | m | fail_rate_at_w | g_lambda |\n";
        out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";

        for (size_t i = 0; i < cases.size();)
        {
            const size_t n = cases[i].n;
            const double eps = cases[i].eps;
            size_t j = i;
            const GridCase* best = nullptr;
            while (j < cases.size() && cases[j].n == n && std::abs(cases[j].eps - eps) < 0.000001)
            {
                if (cases[j].fail == 0 && !best)
                {
                    best = &cases[j];
                }
                ++j;
            }

            if (best)
            {
                const double failRate = static_cast<double>(best->fail) / static_cast<double>(best->pass + best->fail);
                const double g = lambdaTarget - 2.751 * best->eps * static_cast<double>(best->w);
                out << "| " << sizeLabel(n)
                    << " | " << best->eps
                    << " | " << best->w
                    << " | " << best->m
                    << " | " << failRate
                    << " | " << g
                    << " |\n";
            }
            else
            {
                out << "| " << sizeLabel(n)
                    << " | " << eps
                    << " | - | - | - | - |\n";
            }

            i = j;
        }

        out << "\n## All Tried Cases\n\n";
        out << "| Size | eps | w | m | pass | fail | fail_rate | runtime_ms |\n";
        out << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
        for (const auto& c : cases)
        {
            const double total = static_cast<double>(c.pass + c.fail);
            const double failRate = total > 0.0 ? static_cast<double>(c.fail) / total : 0.0;
            out << "| " << sizeLabel(c.n)
                << " | " << c.eps
                << " | " << c.w
                << " | " << c.m
                << " | " << c.pass
                << " | " << c.fail
                << " | " << failRate
                << " | " << c.runtimeMs
                << " |\n";
        }
    }

    const GridCase* firstPass(const std::vector<GridCase>& cases, size_t n, double eps)
    {
        const GridCase* best = nullptr;
        for (const auto& c : cases)
        {
            if (c.n != n || std::abs(c.eps - eps) >= 0.000001)
            {
                continue;
            }
            if (c.fail == 0 && (!best || c.w < best->w))
            {
                best = &c;
            }
        }
        return best;
    }

    void writeSmartReport(
        const std::string& outPath,
        const std::vector<GridCase>& coarse,
        const std::vector<GridCase>& focus,
        size_t coarseRounds,
        size_t focusRounds,
        u64 seedBase,
        double lambdaTarget)
    {
        std::ofstream out(outPath, std::ios::out | std::ios::trunc);
        if (!out)
        {
            throw std::runtime_error("failed to open output file: " + outPath);
        }

        out << "# RB-OKVS Smart G Calibration\n\n";
        out << std::fixed << std::setprecision(4);
        out << "`coarse_rounds=" << coarseRounds << "` "
            << "`focus_rounds=" << focusRounds << "` "
            << "`seed_base=" << seedBase << "` "
            << "`lambda_target=" << lambdaTarget << "`\n\n";
        out << "Note\n";
        out << "- the checker disables the lambda gate so it can measure candidate widths\n";
        out << "- w_min_pass means zero observed failures in this calibration run\n";
        out << "- zero observed failures is not a proof of 2^-lambda failure probability\n\n";

        out << "## Final G\n\n";
        out << "| Size | eps | w_min_pass | m | fail_rate_at_w | g_lambda |\n";
        out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";

        for (size_t i = 0; i < focus.size();)
        {
            const size_t n = focus[i].n;
            const double eps = focus[i].eps;
            const GridCase* best = firstPass(focus, n, eps);
            if (best)
            {
                const double total = static_cast<double>(best->pass + best->fail);
                const double failRate = total > 0.0 ? static_cast<double>(best->fail) / total : 0.0;
                const double g = lambdaTarget - 2.751 * best->eps * static_cast<double>(best->w);
                out << "| " << sizeLabel(n)
                    << " | " << best->eps
                    << " | " << best->w
                    << " | " << best->m
                    << " | " << failRate
                    << " | " << g
                    << " |\n";
            }
            else
            {
                out << "| " << sizeLabel(n)
                    << " | " << eps
                    << " | - | - | - | - |\n";
            }

            while (i < focus.size() && focus[i].n == n && std::abs(focus[i].eps - eps) < 0.000001)
            {
                ++i;
            }
        }

        out << "\n## Coarse Boundary\n\n";
        out << "| Size | eps | w_min_pass | m |\n";
        out << "| --- | ---: | ---: | ---: |\n";
        for (size_t i = 0; i < coarse.size();)
        {
            const size_t n = coarse[i].n;
            const double eps = coarse[i].eps;
            const GridCase* best = firstPass(coarse, n, eps);
            if (best)
            {
                out << "| " << sizeLabel(n)
                    << " | " << best->eps
                    << " | " << best->w
                    << " | " << best->m
                    << " |\n";
            }
            else
            {
                out << "| " << sizeLabel(n)
                    << " | " << eps
                    << " | - | - |\n";
            }

            while (i < coarse.size() && coarse[i].n == n && std::abs(coarse[i].eps - eps) < 0.000001)
            {
                ++i;
            }
        }

        out << "\n## Focused Cases\n\n";
        out << "| Size | eps | w | m | pass | fail | fail_rate | runtime_ms |\n";
        out << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
        for (const auto& c : focus)
        {
            const double total = static_cast<double>(c.pass + c.fail);
            const double failRate = total > 0.0 ? static_cast<double>(c.fail) / total : 0.0;
            out << "| " << sizeLabel(c.n)
                << " | " << c.eps
                << " | " << c.w
                << " | " << c.m
                << " | " << c.pass
                << " | " << c.fail
                << " | " << failRate
                << " | " << c.runtimeMs
                << " |\n";
        }
    }

    int runGrid(int argc, char** argv)
    {
        if (argc < 4)
        {
            throw std::invalid_argument(
                "usage: rbokvs_g_check grid <out.md> <rounds> [seedBase] [lambdaTarget]");
        }

        const std::string out = argv[2];
        const size_t rounds = static_cast<size_t>(std::stoull(argv[3]));
        const u64 seedBase = argc >= 5 ? static_cast<u64>(std::stoull(argv[4])) : 1;
        const double lambdaTarget = argc >= 6 ? std::stod(argv[5]) : 40.0;

        std::vector<GridCase> cases;
        const auto sizes = parseSizes(std::getenv("RB_GRID_SIZES"));
        const auto ws = parseWs(std::getenv("RB_GRID_W"));
        const auto epsOverride = parseDoubles(std::getenv("RB_GRID_EPS"));

        for (size_t n : sizes)
        {
            const auto epsList = epsOverride.empty() ? defaultEps(n) : epsOverride;
            for (double eps : epsList)
            {
                for (size_t w : ws)
                {
                    std::cerr << "[grid] n=" << n << " eps=" << eps << " w=" << w << "\n";
                    cases.push_back(runGridCase(n, eps, w, rounds, seedBase));
                }
            }
        }

        std::sort(cases.begin(), cases.end(), [](const GridCase& a, const GridCase& b)
        {
            return std::tie(a.n, a.eps, a.w) < std::tie(b.n, b.eps, b.w);
        });

        writeGridReport(out, cases, rounds, seedBase, lambdaTarget);
        return 0;
    }

    int runSmart(int argc, char** argv)
    {
        if (argc < 5)
        {
            throw std::invalid_argument(
                "usage: rbokvs_g_check smart <out.md> <coarseRounds> <focusRounds> [seedBase] [lambdaTarget]");
        }

        const std::string out = argv[2];
        const size_t coarseRounds = static_cast<size_t>(std::stoull(argv[3]));
        const size_t focusRounds = static_cast<size_t>(std::stoull(argv[4]));
        const u64 seedBase = argc >= 6 ? static_cast<u64>(std::stoull(argv[5])) : 1;
        const double lambdaTarget = argc >= 7 ? std::stod(argv[6]) : 40.0;

        const auto sizes = parseSizes(std::getenv("RB_GRID_SIZES"));
        const auto epsOverride = parseDoubles(std::getenv("RB_GRID_EPS"));
        const size_t focusRadius = 16;

        std::vector<GridCase> coarse;
        std::vector<GridCase> focus;
        const auto coarseW = rangeW(16, 256, 8);

        for (size_t n : sizes)
        {
            const auto epsList = epsOverride.empty() ? defaultEps(n) : epsOverride;
            for (double eps : epsList)
            {
                for (size_t w : coarseW)
                {
                    std::cerr << "[coarse] n=" << n << " eps=" << eps << " w=" << w << "\n";
                    coarse.push_back(runGridCase(n, eps, w, coarseRounds, seedBase));
                }

                const GridCase* coarseBest = firstPass(coarse, n, eps);
                if (!coarseBest)
                {
                    continue;
                }

                const size_t m = coarseBest->m;
                const size_t lo = std::max<size_t>(16, coarseBest->w > focusRadius ? coarseBest->w - focusRadius : 16);
                const size_t hi = std::min<size_t>({256, m, coarseBest->w + focusRadius});
                for (size_t w : rangeW(lo, hi, 4))
                {
                    std::cerr << "[focus] n=" << n << " eps=" << eps << " w=" << w << "\n";
                    focus.push_back(runGridCase(n, eps, w, focusRounds, seedBase + 0xfeed0000ULL));
                }
            }
        }

        std::sort(coarse.begin(), coarse.end(), [](const GridCase& a, const GridCase& b)
        {
            return std::tie(a.n, a.eps, a.w) < std::tie(b.n, b.eps, b.w);
        });
        std::sort(focus.begin(), focus.end(), [](const GridCase& a, const GridCase& b)
        {
            return std::tie(a.n, a.eps, a.w) < std::tie(b.n, b.eps, b.w);
        });

        writeSmartReport(out, coarse, focus, coarseRounds, focusRounds, seedBase, lambdaTarget);
        return 0;
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
        if (argc >= 2 && std::string(argv[1]) == "grid")
        {
            return runGrid(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "smart")
        {
            return runSmart(argc, argv);
        }

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
