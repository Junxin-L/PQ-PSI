#include "frontend/permutation.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <sys/utsname.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

using namespace osuCrypto;

namespace
{
	std::string nowIsoLike()
	{
		const auto now = std::chrono::system_clock::now();
		const auto tt = std::chrono::system_clock::to_time_t(now);
		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &tt);
#else
		localtime_r(&tt, &tm);
#endif
		char buf[64];
		std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
		return std::string(buf);
	}

	std::string unameLine()
	{
		struct utsname u {};
		if (uname(&u) != 0)
		{
			return "unknown";
		}
		return std::string(u.sysname) + " " + u.release + " " + u.version + " " + u.machine;
	}

	std::string cpuModel()
	{
#if defined(__APPLE__)
		char buf[256];
		size_t len = sizeof(buf);
		if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0)
		{
			return std::string(buf);
		}
		return "unknown (sysctl machdep.cpu.brand_string failed)";
#else
		return "unknown";
#endif
	}

	struct StatsNs
	{
		double mean = 0;
		double median = 0;
		double p95 = 0;
		double p99 = 0;
		double min = 0;
		double max = 0;
	};

	StatsNs summarize(std::vector<double> v)
	{
		StatsNs s;
		if (v.empty())
		{
			return s;
		}

		std::sort(v.begin(), v.end());
		const auto n = v.size();
		const auto idx = [&](double q)
		{
			size_t i = static_cast<size_t>(q * static_cast<double>(n - 1));
			if (i >= n) i = n - 1;
			return i;
		};

		s.mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(n);
		s.median = v[idx(0.50)];
		s.p95 = v[idx(0.95)];
		s.p99 = v[idx(0.99)];
		s.min = v.front();
		s.max = v.back();
		return s;
	}

	inline double nsToUs(double ns) { return ns / 1e3; }
	inline double nsToMs(double ns) { return ns / 1e6; }
}

int main(int argc, char** argv)
{
	const std::string outPath = (argc > 1) ? argv[1] : "build-x86/construction_permutation_benchmark.txt";

	// This tracks pqpsi settings and lambda constraint: s>3*lambda and n-s>3*lambda
	const size_t nBits = Keccak_size_bit;
	const size_t NBits = KEM_key_size_bit;
	const size_t sBits = 1400;
	const size_t lambdaBits = 40;
	const size_t warmupRounds = 20;
	const size_t benchRounds = 400;
	const u64 rngSeed = 0x5EED123456789ABCuLL;

	ConstructionPermutation P(
		nBits,
		NBits,
		sBits,
		Keccak1600Adapter::pi,
		Keccak1600Adapter::pi_inv,
		lambdaBits);

	std::mt19937_64 rng(rngSeed);
	std::vector<Bits> states(benchRounds + warmupRounds, Bits(NBits, 0));
	for (auto& st : states)
	{
		for (auto& b : st)
		{
			b = static_cast<u8>(rng() & 1ULL);
		}
	}

	for (size_t i = 0; i < warmupRounds; ++i)
	{
		auto y = P.encrypt(states[i]);
		auto z = P.decrypt(y);
		(void)z;
	}

	std::vector<double> encNs;
	std::vector<double> decNs;
	encNs.reserve(benchRounds);
	decNs.reserve(benchRounds);

	size_t roundtripFail = 0;
	for (size_t i = 0; i < benchRounds; ++i)
	{
		const auto& x = states[warmupRounds + i];

		const auto t0 = std::chrono::steady_clock::now();
		auto y = P.encrypt(x);
		const auto t1 = std::chrono::steady_clock::now();
		auto z = P.decrypt(y);
		const auto t2 = std::chrono::steady_clock::now();

		encNs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
		decNs.push_back(std::chrono::duration<double, std::nano>(t2 - t1).count());
		if (z != x)
		{
			++roundtripFail;
		}
	}

	const auto enc = summarize(encNs);
	const auto dec = summarize(decNs);
	const double bytesPerOp = static_cast<double>(NBits) / 8.0;
	const double encMBps = (bytesPerOp / (enc.mean / 1e9)) / (1024.0 * 1024.0);
	const double decMBps = (bytesPerOp / (dec.mean / 1e9)) / (1024.0 * 1024.0);

	std::ofstream out(outPath, std::ios::out | std::ios::trunc);
	if (!out)
	{
		std::cerr << "failed to open output file: " << outPath << "\n";
		return 1;
	}

	out << "ConstructionPermutation benchmark (s=" << sBits << ")\n";
	out << "timestamp: " << nowIsoLike() << "\n";
	out << "host_uname: " << unameLine() << "\n";
	out << "cpu_model: " << cpuModel() << "\n";
	out << "hw_threads: " << std::thread::hardware_concurrency() << "\n";
	out << "compiler: " << __VERSION__ << "\n";
	out << "keccak_backend: thirdparty/KeccakTools/Sources/Keccak-f.*\n";
	out << "what_is_measured: permutation.h ConstructionPermutation encrypt/decrypt final parameters with s=" << sBits << "\n";
	out << "selected_s_bits: " << sBits << "\n";
	out << "arch_note: run under x86_64 if on Apple Silicon\n";
	out << "\n";

	out << "parameters\n";
	out << "n_bits: " << nBits << "\n";
	out << "N_bits: " << NBits << "\n";
	out << "s_bits: " << sBits << "\n";
	out << "lambda_bits: " << lambdaBits << "\n";
	out << "constraint_check: s>3*lambda and n-s>3*lambda\n";
	out << "rounds_in_construction: " << P.rounds() << "\n";
	out << "warmup_rounds: " << warmupRounds << "\n";
	out << "benchmark_rounds: " << benchRounds << "\n";
	out << "rng_seed: 0x" << std::hex << rngSeed << std::dec << "\n";
	out << "state_bits_per_op: " << NBits << "\n";
	out << "state_bytes_per_op: " << static_cast<u64>(bytesPerOp) << "\n";
	out << "\n";

	out << std::fixed << std::setprecision(2);
	out << "encrypt_mean_us: " << nsToUs(enc.mean) << "\n";
	out << "encrypt_median_us: " << nsToUs(enc.median) << "\n";
	out << "encrypt_p95_us: " << nsToUs(enc.p95) << "\n";
	out << "encrypt_p99_us: " << nsToUs(enc.p99) << "\n";
	out << "encrypt_min_us: " << nsToUs(enc.min) << "\n";
	out << "encrypt_max_us: " << nsToUs(enc.max) << "\n";
	out << "encrypt_mean_ms: " << nsToMs(enc.mean) << "\n";
	out << "encrypt_MBps_mean: " << encMBps << "\n";
	out << "\n";

	out << "decrypt_mean_us: " << nsToUs(dec.mean) << "\n";
	out << "decrypt_median_us: " << nsToUs(dec.median) << "\n";
	out << "decrypt_p95_us: " << nsToUs(dec.p95) << "\n";
	out << "decrypt_p99_us: " << nsToUs(dec.p99) << "\n";
	out << "decrypt_min_us: " << nsToUs(dec.min) << "\n";
	out << "decrypt_max_us: " << nsToUs(dec.max) << "\n";
	out << "decrypt_mean_ms: " << nsToMs(dec.mean) << "\n";
	out << "decrypt_MBps_mean: " << decMBps << "\n";
	out << "\n";

	out << "correctness\n";
	out << "roundtrip_failures: " << roundtripFail << "\n";
	out << "roundtrip_ok: " << ((roundtripFail == 0) ? "yes" : "no") << "\n";

	out.close();
	std::cout << "wrote benchmark report: " << outPath << "\n";
	return 0;
}
