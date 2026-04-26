#include "frontend/pqpsi/pqpsi.h"
#include "frontend/obf-mlkem/backend/MlKem.h"
#include "frontend/obf-mlkem/codec/Kemeleon.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace osuCrypto;

namespace
{
    bool argEq(const char* a, const char* b)
    {
        return std::string(a ? a : "") == std::string(b ? b : "");
    }

    u64 parseU64(const char* s, u64 fallback)
    {
        if (s == nullptr || *s == '\0')
            return fallback;
        return static_cast<u64>(std::stoull(s));
    }

    double parseDouble(const char* s, double fallback)
    {
        if (s == nullptr || *s == '\0')
            return fallback;
        return std::stod(s);
    }

    void setEnv(const char* key, const std::string& val)
    {
#if defined(_WIN32)
        _putenv_s(key, val.c_str());
#else
        setenv(key, val.c_str(), 1);
#endif
    }

    std::string fmt(double v, int prec = 2)
    {
        std::ostringstream os;
        os << std::fixed << std::setprecision(prec) << v;
        return os.str();
    }

    double meanOf(const std::vector<double>& xs)
    {
        if (xs.empty())
            return 0.0;
        return std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
    }

    struct NetCfg
    {
        double delayMs = 0.0;
        double bwMBps = 0.0;
    };

    struct PqArgs
    {
        u64 setSize = 256;
        u64 warmups = 1;
        u64 rounds = 5;
        u64 portBase = 43000;
        u64 hits = std::numeric_limits<u64>::max();
        NetCfg net;
        RbCfg rb{};
    };

    struct PqRow
    {
        bool ok = false;
        u64 got = 0;
        u64 want = 0;
        double runtimeMs = 0.0;
        double commKB = 0.0;
        PqPsiRunProfile prof{};
    };

    PqArgs parsePqArgs(int argc, char** argv, int start)
    {
        PqArgs args;
        int i = start;
        if (argc > i) args.setSize = parseU64(argv[i++], args.setSize);
        if (argc > i) args.warmups = parseU64(argv[i++], args.warmups);
        if (argc > i) args.rounds = parseU64(argv[i++], args.rounds);
        if (argc > i) args.portBase = parseU64(argv[i++], args.portBase);

        while (i < argc)
        {
            if (argEq(argv[i], "--rb-lambda") && i + 1 < argc)
                args.rb.lambda = parseU64(argv[++i], args.rb.lambda);
            else if (argEq(argv[i], "--rb-eps") && i + 1 < argc)
                args.rb.eps = parseDouble(argv[++i], args.rb.eps);
            else if (argEq(argv[i], "--rb-cols") && i + 1 < argc)
                args.rb.columns = parseU64(argv[++i], args.rb.columns);
            else if (argEq(argv[i], "--rb-w") && i + 1 < argc)
                args.rb.bandWidth = parseU64(argv[++i], args.rb.bandWidth);
            else if (argEq(argv[i], "--hits") && i + 1 < argc)
                args.hits = parseU64(argv[++i], args.hits);
            else if (argEq(argv[i], "--misses") && i + 1 < argc)
            {
                const u64 miss = parseU64(argv[++i], 0);
                args.hits = miss > args.setSize ? 0 : args.setSize - miss;
            }
            else if (argEq(argv[i], "--delay-ms") && i + 1 < argc)
                args.net.delayMs = parseDouble(argv[++i], args.net.delayMs);
            else if (argEq(argv[i], "--bw-mbps") && i + 1 < argc)
                args.net.bwMBps = parseDouble(argv[++i], args.net.bwMBps);
            else if (argEq(argv[i], "--single-thread"))
                args.rb.multiThread = false;
            else if (argEq(argv[i], "--multi-thread"))
                args.rb.multiThread = true;
            else
                throw std::invalid_argument(std::string("unknown arg: ") + argv[i]);
            ++i;
        }
        return args;
    }

    PqRow runPqOne(const PqArgs& args, u64 portBase)
    {
        setEnv("PQPSI_PORT_BASE", std::to_string(portBase));
        setEnv("PQPSI_TRACE", "0");
        setEnv("PQPSI_SIM_NET_DELAY_MS", fmt(args.net.delayMs, 3));
        setEnv("PQPSI_SIM_NET_BW_MBPS", fmt(args.net.bwMBps, 3));

        PqRow row;
        const auto t0 = std::chrono::steady_clock::now();
        row.ok = rbRun(args.setSize, row.got, row.want, &args.rb, &row.prof, args.hits);
        const auto t1 = std::chrono::steady_clock::now();
        row.runtimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        row.commKB =
            (row.prof.party0.networkSendBytes + row.prof.party1.networkSendBytes) / 1024.0;
        return row;
    }

    int runPqPsi(int argc, char** argv, int start)
    {
        const auto args = parsePqArgs(argc, argv, start);
        std::vector<PqRow> rows;
        rows.reserve(args.rounds);

        for (u64 i = 0; i < args.warmups; ++i)
        {
            (void)runPqOne(args, args.portBase + i * 100);
        }
        for (u64 i = 0; i < args.rounds; ++i)
        {
            rows.push_back(runPqOne(args, args.portBase + (args.warmups + i) * 100));
        }

        bool ok = true;
        std::vector<double> runMs, commKB, recvMs, sendMs;
        for (const auto& row : rows)
        {
            ok = ok && row.ok;
            runMs.push_back(row.runtimeMs);
            commKB.push_back(row.commKB);
            recvMs.push_back(row.prof.party0.totalMs);
            sendMs.push_back(row.prof.party1.totalMs);
        }

        std::cout
            << "pqpsi-rbokvs\n"
            << "set=" << args.setSize
            << " rounds=" << args.rounds
            << " hits=" << (args.hits == std::numeric_limits<u64>::max() ? args.setSize - 1 : args.hits)
            << " thread_mode=" << (args.rb.multiThread ? "multi" : "single")
            << " status=" << (ok ? "ok" : "fail") << "\n"
            << "runtime_ms=" << fmt(meanOf(runMs))
            << " comm_kb=" << fmt(meanOf(commKB))
            << " recv_ms=" << fmt(meanOf(recvMs))
            << " send_ms=" << fmt(meanOf(sendMs))
            << "\n";
        return ok ? 0 : 1;
    }

    struct RbArgs
    {
        u64 n = 256;
        double eps = 0.10;
        u64 w = 64;
        u64 rounds = 20;
        bool multiThread = true;
    };

    RbArgs parseRbArgs(int argc, char** argv, int start)
    {
        RbArgs args;
        int i = start;
        if (argc > i) args.n = parseU64(argv[i++], args.n);
        if (argc > i) args.eps = parseDouble(argv[i++], args.eps);
        if (argc > i) args.w = parseU64(argv[i++], args.w);
        if (argc > i && argv[i][0] != '-') args.rounds = parseU64(argv[i++], args.rounds);
        while (i < argc)
        {
            if (argEq(argv[i], "--single-thread"))
                args.multiThread = false;
            else if (argEq(argv[i], "--multi-thread"))
                args.multiThread = true;
            else
                throw std::invalid_argument(std::string("unknown arg: ") + argv[i]);
            ++i;
        }
        return args;
    }

    bool runRbOne(const RbArgs& args, u64 seed, double& runMs)
    {
        PRNG prng(toBlock(0x72626f6b7673ULL, seed));
        std::vector<block> keys(args.n);
        std::vector<block> vals(args.n);
        for (size_t i = 0; i < args.n; ++i)
        {
            keys[i] = prng.get<block>() ^ toBlock(seed, static_cast<u64>(i + 1));
            vals[i] = prng.get<block>() ^ toBlock(0xfeed0000ULL + seed, static_cast<u64>(i));
        }

        RBParams rb;
        rb.lambda = 1;
        rb.eps = args.eps;
        rb.bandWidth = args.w;
        rb.multiThread = args.multiThread;
        rb.columns = static_cast<size_t>(std::ceil((1.0 + args.eps) * static_cast<double>(std::max<u64>(args.n, 1))));

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

        if (!encOk || got.size() != vals.size())
            return false;
        for (size_t i = 0; i < vals.size(); ++i)
        {
            if (std::memcmp(&got[i], &vals[i], sizeof(block)) != 0)
                return false;
        }
        return true;
    }

    int runRbOkvs(int argc, char** argv, int start)
    {
        const auto args = parseRbArgs(argc, argv, start);
        u64 pass = 0;
        std::vector<double> runMs;
        runMs.reserve(args.rounds);

        for (u64 i = 0; i < args.rounds; ++i)
        {
            double ms = 0.0;
            const bool ok = runRbOne(args, i + 1, ms);
            runMs.push_back(ms);
            pass += ok ? 1 : 0;
        }

        const double g40 = 40.0 - 2.751 * args.eps * static_cast<double>(args.w);
        std::cout
            << "rbokvs\n"
            << "n=" << args.n
            << " eps=" << fmt(args.eps, 2)
            << " w=" << args.w
            << " rounds=" << args.rounds
            << " thread_mode=" << (args.multiThread ? "multi" : "single")
            << " pass=" << pass << "/" << args.rounds
            << " g_40=" << fmt(g40)
            << " runtime_ms=" << fmt(meanOf(runMs))
            << "\n";
        return pass == args.rounds ? 0 : 1;
    }

    void fillSeed(span<u8> seed, u8 base)
    {
        for (u64 i = 0; i < seed.size(); ++i)
        {
            seed[i] = static_cast<u8>(base + i);
        }
    }

    void kemeleonCheckMode(MlKem::Mode mode)
    {
        MlKem kem(mode);
        Kemeleon codec(mode);

        std::array<u8, MlKem::KeyGenSeedSize> keySeed;
        std::array<u8, MlKem::EncapSeedSize> encSeed;
        fillSeed(keySeed, 0x20);
        fillSeed(encSeed, 0x80);

        MlKem::KeyPair pair;
        std::vector<u8> keyData;
        bool keyOk = false;
        for (u64 i = 0; i < 256 && !keyOk; ++i)
        {
            keySeed[0] = static_cast<u8>(0x20 + i);
            pair = kem.keyGen(keySeed);
            keyOk = codec.encodeKey(pair.publicKey, keyData);
        }
        if (!keyOk)
            throw std::runtime_error("kemeleon key encode failed");

        MlKem::EncapResult enc;
        std::vector<u8> cipherData;
        bool cipherOk = false;
        for (u64 i = 0; i < 256 && !cipherOk; ++i)
        {
            encSeed[0] = static_cast<u8>(0x80 + i);
            enc = kem.encaps(pair.publicKey, encSeed);
            for (u64 j = 0; j < 4096 && !cipherOk; ++j)
            {
                cipherOk = codec.encodeCipher(enc.cipherText, cipherData);
            }
        }
        if (!cipherOk)
            throw std::runtime_error("kemeleon cipher encode failed");

        std::vector<u8> pkOut;
        std::vector<u8> ctOut;
        if (!codec.decodeKey(keyData, pkOut) || pkOut != pair.publicKey)
            throw std::runtime_error("kemeleon decodeKey failed");
        if (!codec.decodeCipher(cipherData, ctOut) || ctOut != enc.cipherText)
            throw std::runtime_error("kemeleon decodeCipher failed");
    }

    int runKemeleon()
    {
        kemeleonCheckMode(MlKem::Mode::MlKem512);
        kemeleonCheckMode(MlKem::Mode::MlKem768);
        kemeleonCheckMode(MlKem::Mode::MlKem1024);
        std::cout << "kemeleon\nstatus=ok\n";
        return 0;
    }

    void usage(const char* prog)
    {
        std::cout
            << "Usage\n"
            << "  " << prog << " pqpsi-rbokvs [setSize] [warmups] [rounds] [portBase] [--hits v] [--rb-eps v] [--rb-w v] [--rb-cols v] [--rb-lambda v] [--single-thread|--multi-thread]\n"
            << "  " << prog << " rbokvs [setSize] [eps] [w] [rounds] [--single-thread|--multi-thread]\n"
            << "  " << prog << " kemeleon\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc <= 1 || argEq(argv[1], "-h") || argEq(argv[1], "--help") || argEq(argv[1], "help"))
        {
            usage(argv[0]);
            return 0;
        }

        if (argEq(argv[1], "pqpsi-rbokvs"))
            return runPqPsi(argc, argv, 2);
        if (argEq(argv[1], "rbokvs"))
            return runRbOkvs(argc, argv, 2);
        if (argEq(argv[1], "kemeleon"))
            return runKemeleon();

        throw std::invalid_argument(std::string("unknown test: ") + argv[1]);
    }
    catch (const std::exception& e)
    {
        std::cerr << "pqpsi_tests failed: " << e.what() << "\n";
        return 1;
    }
}
