#include "pqpsi/pqpsi.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
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

	struct StatsUs
	{
		double mean = 0.0;
		double median = 0.0;
		double p95 = 0.0;
		double p99 = 0.0;
		double min = 0.0;
		double max = 0.0;
	};

	StatsUs summarize(std::vector<double> v)
	{
		StatsUs s;
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

	u64 parseU64(char* s, u64 fallback)
	{
		if (s == nullptr || *s == '\0')
		{
			return fallback;
		}
		try
		{
			return static_cast<u64>(std::stoull(s));
		}
		catch (...)
		{
			return fallback;
		}
	}

	double parseDouble(const char* s, double fallback)
	{
		if (s == nullptr || *s == '\0')
		{
			return fallback;
		}
		try
		{
			return std::stod(s);
		}
		catch (...)
		{
			return fallback;
		}
	}

	std::string getenvOr(const char* key, const char* fallback)
	{
		const char* v = std::getenv(key);
		if (v == nullptr || *v == '\0')
		{
			return std::string(fallback);
		}
		return std::string(v);
	}

	std::string trimCopy(const std::string& s)
	{
		size_t b = 0;
		while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
		size_t e = s.size();
		while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
		return s.substr(b, e - b);
	}

	void loadNetworkSettingsFile(
		const std::string& path,
		double& simDelayMs,
		double& simBwMBps,
		std::string& netType,
		std::string& netTopology,
		std::string& linkSpeed,
		std::string& netNote)
	{
		if (path.empty())
		{
			return;
		}

		std::ifstream in(path);
		if (!in)
		{
			throw std::runtime_error("failed to open network settings file: " + path);
		}

		std::string line;
		while (std::getline(in, line))
		{
			line = trimCopy(line);
			if (line.empty() || line[0] == '#')
			{
				continue;
			}
			const auto pos = line.find('=');
			if (pos == std::string::npos)
			{
				continue;
			}
			const std::string key = trimCopy(line.substr(0, pos));
			const std::string val = trimCopy(line.substr(pos + 1));

			if (key == "sim_delay_ms")
				simDelayMs = parseDouble(val.c_str(), simDelayMs);
			else if (key == "sim_bw_MBps")
				simBwMBps = parseDouble(val.c_str(), simBwMBps);
			else if (key == "network_type")
				netType = val;
			else if (key == "network_topology")
				netTopology = val;
			else if (key == "link_speed")
				linkSpeed = val;
			else if (key == "network_note")
				netNote = val;
		}
	}

	bool isNumberString(const char* s)
	{
		if (s == nullptr || *s == '\0')
		{
			return false;
		}
		char* end = nullptr;
		std::strtod(s, &end);
		return end != s && end != nullptr && *end == '\0';
	}
}

int main(int argc, char** argv)
{
	const std::string outPath = (argc > 1) ? argv[1] : "build-x86/pqpsi_rbokvs_benchmark.txt";
	const u64 setSize = (argc > 2) ? parseU64(argv[2], 64) : 64;
	const u64 warmups = (argc > 3) ? parseU64(argv[3], 1) : 1;
	const u64 rounds = (argc > 4) ? parseU64(argv[4], 5) : 5;
	const u64 portBaseSeed = (argc > 5) ? parseU64(argv[5], 23000) : 23000;
	const bool arg6IsNum = (argc > 6) ? isNumberString(argv[6]) : false;
	const bool arg7IsNum = (argc > 7) ? isNumberString(argv[7]) : false;
	std::string netSettingsPath;
	if (argc > 8)
	{
		netSettingsPath = argv[8];
	}
	else if (argc > 6 && !arg6IsNum)
	{
		netSettingsPath = argv[6];
	}
	else if (argc > 7 && !arg7IsNum)
	{
		netSettingsPath = argv[7];
	}

	double simDelayMs = parseDouble(std::getenv("PQPSI_SIM_NET_DELAY_MS"), 0.0);
	double simBwMBps = parseDouble(std::getenv("PQPSI_SIM_NET_BW_MBPS"), 0.0);
	std::string netType = getenvOr("PQPSI_BENCH_NET_TYPE", "loopback");
	std::string netTopology = getenvOr("PQPSI_BENCH_TOPOLOGY", "same-host");
	std::string linkSpeed = getenvOr("PQPSI_BENCH_LINK", "N/A");
	std::string netNote = getenvOr("PQPSI_BENCH_NET_NOTE", "no WAN emulation");

	loadNetworkSettingsFile(netSettingsPath, simDelayMs, simBwMBps, netType, netTopology, linkSpeed, netNote);

	if (argc > 6 && arg6IsNum) simDelayMs = parseDouble(argv[6], simDelayMs);
	if (argc > 7 && arg7IsNum) simBwMBps = parseDouble(argv[7], simBwMBps);

	std::vector<double> runUs;
	runUs.reserve(rounds);
	std::vector<double> p0TotalMs, p0KemKeyGenMs, p0PermuteMs, p0OkvsEncMs, p0OkvsDecMs, p0SendMs, p0RecvMs, p0KemCoreMs, p0PermDecMs;
	std::vector<double> p1TotalMs, p1KemKeyGenMs, p1PermuteMs, p1OkvsEncMs, p1OkvsDecMs, p1SendMs, p1RecvMs, p1KemCoreMs, p1PermDecMs;
	p0TotalMs.reserve(rounds); p0KemKeyGenMs.reserve(rounds); p0PermuteMs.reserve(rounds); p0OkvsEncMs.reserve(rounds);
	p0OkvsDecMs.reserve(rounds); p0SendMs.reserve(rounds); p0RecvMs.reserve(rounds); p0KemCoreMs.reserve(rounds); p0PermDecMs.reserve(rounds);
	p1TotalMs.reserve(rounds); p1KemKeyGenMs.reserve(rounds); p1PermuteMs.reserve(rounds); p1OkvsEncMs.reserve(rounds);
	p1OkvsDecMs.reserve(rounds); p1SendMs.reserve(rounds); p1RecvMs.reserve(rounds); p1KemCoreMs.reserve(rounds); p1PermDecMs.reserve(rounds);

	u64 passCount = 0;
	u64 failCount = 0;
	u64 mismatchCount = 0;
	u64 portRetryCount = 0;
	const u64 maxPortRetriesPerRound = 32;

	for (u64 i = 0; i < warmups + rounds; ++i)
	{
		bool ok = false;
		u64 got = 0;
		u64 expected = 0;
		PqPsiRunProfile profile{};
		bool done = false;
		u64 tryIdx = 0;
		std::chrono::steady_clock::time_point t0;
		std::chrono::steady_clock::time_point t1;
		while (!done && tryIdx <= maxPortRetriesPerRound)
		{
			const u64 portBase = portBaseSeed + i * 17 + tryIdx * 101;
			const std::string portBaseStr = std::to_string(portBase);
			const std::string runTagStr = std::to_string(i) + "_" + std::to_string(tryIdx) + "_" + portBaseStr;
			const std::string simDelayStr = std::to_string(simDelayMs);
			const std::string simBwStr = std::to_string(simBwMBps);
			setenv("PQPSI_PORT_BASE", portBaseStr.c_str(), 1);
			setenv("PQPSI_RUN_TAG", runTagStr.c_str(), 1);
			setenv("PQPSI_TRACE", "0", 1);
			setenv("PQPSI_SIM_NET_DELAY_MS", simDelayStr.c_str(), 1);
			setenv("PQPSI_SIM_NET_BW_MBPS", simBwStr.c_str(), 1);

			try
			{
				t0 = std::chrono::steady_clock::now();
				ok = PqPsi_RbOkvs_RunCheck(setSize, got, expected, &profile);
				t1 = std::chrono::steady_clock::now();
				done = true;
			}
			catch (const std::runtime_error& e)
			{
				const std::string msg = e.what();
				if (msg.find("Address already in use") != std::string::npos)
				{
					++portRetryCount;
					++tryIdx;
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					continue;
				}
				throw;
			}
		}
		if (!done)
		{
			throw std::runtime_error("pqpsi_rbokvs_bench: ports busy after retries");
		}

		if (i >= warmups)
		{
			const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
			runUs.push_back(us);
			p0TotalMs.push_back(profile.party0.totalMs);
			p0KemKeyGenMs.push_back(profile.party0.kemKeyGenMs);
			p0PermuteMs.push_back(profile.party0.permuteMs);
			p0OkvsEncMs.push_back(profile.party0.okvsEncodeMs);
			p0OkvsDecMs.push_back(profile.party0.okvsDecodeMs);
			p0SendMs.push_back(profile.party0.networkSendMs);
			p0RecvMs.push_back(profile.party0.networkRecvMs);
			p0KemCoreMs.push_back(profile.party0.kemCoreMs);
			p0PermDecMs.push_back(profile.party0.permDecryptMs);
			p1TotalMs.push_back(profile.party1.totalMs);
			p1KemKeyGenMs.push_back(profile.party1.kemKeyGenMs);
			p1PermuteMs.push_back(profile.party1.permuteMs);
			p1OkvsEncMs.push_back(profile.party1.okvsEncodeMs);
			p1OkvsDecMs.push_back(profile.party1.okvsDecodeMs);
			p1SendMs.push_back(profile.party1.networkSendMs);
			p1RecvMs.push_back(profile.party1.networkRecvMs);
			p1KemCoreMs.push_back(profile.party1.kemCoreMs);
			p1PermDecMs.push_back(profile.party1.permDecryptMs);

			if (ok)
			{
				++passCount;
			}
			else
			{
				++failCount;
			}
			if (got != expected)
			{
				++mismatchCount;
			}
		}
	}

	const auto stats = summarize(runUs);
	const auto p0Total = summarize(p0TotalMs);
	const auto p0KemKeyGen = summarize(p0KemKeyGenMs);
	const auto p0Permute = summarize(p0PermuteMs);
	const auto p0OkvsEnc = summarize(p0OkvsEncMs);
	const auto p0OkvsDec = summarize(p0OkvsDecMs);
	const auto p0Send = summarize(p0SendMs);
	const auto p0Recv = summarize(p0RecvMs);
	const auto p0KemCore = summarize(p0KemCoreMs);
	const auto p0PermDec = summarize(p0PermDecMs);
	const auto p1Total = summarize(p1TotalMs);
	const auto p1KemKeyGen = summarize(p1KemKeyGenMs);
	const auto p1Permute = summarize(p1PermuteMs);
	const auto p1OkvsEnc = summarize(p1OkvsEncMs);
	const auto p1OkvsDec = summarize(p1OkvsDecMs);
	const auto p1Send = summarize(p1SendMs);
	const auto p1Recv = summarize(p1RecvMs);
	const auto p1KemCore = summarize(p1KemCoreMs);
	const auto p1PermDec = summarize(p1PermDecMs);

	std::ofstream out(outPath, std::ios::out | std::ios::trunc);
	if (!out)
	{
		std::cerr << "failed to open output file: " << outPath << "\n";
		return 1;
	}

	out << "PQPSI RB-OKVS benchmark\n";
	out << "timestamp: " << nowIsoLike() << "\n";
	out << "host_uname: " << unameLine() << "\n";
	out << "cpu_model: " << cpuModel() << "\n";
	out << "hw_threads: " << std::thread::hardware_concurrency() << "\n";
	out << "compiler: " << __VERSION__ << "\n";
#if defined(NDEBUG)
	out << "build_mode: release\n";
#else
	out << "build_mode: debug\n";
#endif
	out << "\n";

	out << "protocol_config\n";
	out << "parties: 2\n";
	out << "kem_mode: MlKem512\n";
	out << "okvs_type: RandomBandOkvs\n";
	out << "set_size_per_party: " << setSize << "\n";
	out << "\n";

	out << "network_config\n";
	out << "transport: BtEndpoint over local loopback\n";
	out << "host: localhost\n";
	out << "network_type: " << netType << "\n";
	out << "network_topology: " << netTopology << "\n";
	out << "link_speed: " << linkSpeed << "\n";
	out << "network_note: " << netNote << "\n";
	out << "network_settings_file: " << (netSettingsPath.empty() ? "none" : netSettingsPath) << "\n";
	out << "sim_net_delay_ms: " << simDelayMs << "\n";
	out << "sim_net_bw_MBps: " << simBwMBps << "\n";
	out << "port_base_seed: " << portBaseSeed << "\n";
	out << "per_round_port_step: 17\n";
	out << "port_retry_step: 101\n";
	out << "port_retry_total: " << portRetryCount << "\n";
	out << "trace_output: disabled (PQPSI_TRACE=0)\n";
	out << "note: benchmark process does not auto-measure physical link speed\n";
	out << "\n";

	out << "benchmark_config\n";
	out << "warmup_rounds: " << warmups << "\n";
	out << "measured_rounds: " << rounds << "\n";
	out << "timing_unit_primary: ms\n";
	out << "\n";

	out << std::fixed << std::setprecision(2);
	out << "runtime_mean_ms: " << (stats.mean / 1000.0) << "\n";
	out << "runtime_median_ms: " << (stats.median / 1000.0) << "\n";
	out << "runtime_p95_ms: " << (stats.p95 / 1000.0) << "\n";
	out << "runtime_p99_ms: " << (stats.p99 / 1000.0) << "\n";
	out << "runtime_min_ms: " << (stats.min / 1000.0) << "\n";
	out << "runtime_max_ms: " << (stats.max / 1000.0) << "\n";
	out << "\n";

	auto pct = [](double part, double whole)
	{
		if (whole <= 0.0) return 0.0;
		return (part * 100.0) / whole;
	};

	auto printPartyHuman = [&](const char* partyName,
		const StatsUs& total,
		const StatsUs& kemKeyGen,
		const StatsUs& permute,
		const StatsUs& okvsEnc,
		const StatsUs& okvsDec,
		const StatsUs& netSend,
		const StatsUs& netRecv,
		const StatsUs& kemCore,
		const StatsUs& permDec)
	{
		out << partyName << " (mean total " << total.mean << " ms)\n";
		out << "  keygen: " << kemKeyGen.mean << " ms (" << pct(kemKeyGen.mean, total.mean) << "%)\n";
		out << "  permute: " << permute.mean << " ms (" << pct(permute.mean, total.mean) << "%)\n";
		out << "  permute_inverse: " << permDec.mean << " ms (" << pct(permDec.mean, total.mean) << "%)\n";
		out << "  kem_ops (Encaps/Decaps): " << kemCore.mean << " ms (" << pct(kemCore.mean, total.mean) << "%)\n";
		out << "  okvs_encode: " << okvsEnc.mean << " ms (" << pct(okvsEnc.mean, total.mean) << "%)\n";
		out << "  okvs_decode: " << okvsDec.mean << " ms (" << pct(okvsDec.mean, total.mean) << "%)\n";
		out << "  network_send (socket send + simulated net delay if enabled): " << netSend.mean << " ms (" << pct(netSend.mean, total.mean) << "%)\n";
		out << "  network_recv (socket recv wait): " << netRecv.mean << " ms (" << pct(netRecv.mean, total.mean) << "%)\n";

		out << "\n";
	};

	printPartyHuman("party0", p0Total, p0KemKeyGen, p0Permute, p0OkvsEnc, p0OkvsDec, p0Send, p0Recv, p0KemCore, p0PermDec);
	printPartyHuman("party1", p1Total, p1KemKeyGen, p1Permute, p1OkvsEnc, p1OkvsDec, p1Send, p1Recv, p1KemCore, p1PermDec);

	out << std::setprecision(0);
	out << "correctness_pass_rounds: " << passCount << "\n";
	out << "correctness_fail_rounds: " << failCount << "\n";
	out << "intersection_mismatch_rounds: " << mismatchCount << "\n";
	out << "overall_status: " << ((failCount == 0 && mismatchCount == 0) ? "ok" : "needs_check") << "\n";

	out.close();
	std::cout << "wrote benchmark report: " << outPath << "\n";
	return 0;
}
