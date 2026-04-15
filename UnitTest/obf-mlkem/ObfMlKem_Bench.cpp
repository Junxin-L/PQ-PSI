#include "frontend/obf-mlkem/backend/MlKem.h"
#include "frontend/obf-mlkem/codec/Kemeleon.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/utsname.h>
#include <vector>

using namespace osuCrypto;

namespace
{
	using Clock = std::chrono::steady_clock;

	struct BenchConfig
	{
		u64 keySearchTries = 128;
		u64 cipherWarmupTries = 32768;
		u64 cipherBenchTries = 4096;
		u64 kemRounds = 200;
		u64 codecRounds = 200;
		std::string outputPath = "build-x86/obf_mlkem_benchmark.txt";
	};

	struct Reporter
	{
		explicit Reporter(std::ostream& file)
			: mFile(file)
		{
		}

		template<typename T>
		Reporter& operator<<(const T& x)
		{
			std::cout << x;
			mFile << x;
			return *this;
		}

		Reporter& operator<<(std::ostream& (*manip)(std::ostream&))
		{
			manip(std::cout);
			manip(mFile);
			return *this;
		}

	private:
		std::ostream& mFile;
	};

	std::string modeName(MlKem::Mode mode)
	{
		switch (mode)
		{
		case MlKem::Mode::MlKem512: return "ML-KEM-512";
		case MlKem::Mode::MlKem768: return "ML-KEM-768";
		case MlKem::Mode::MlKem1024: return "ML-KEM-1024";
		default: throw std::runtime_error("Unexpected ML-KEM mode");
			}
		}

		std::string nowString()
		{
			const std::time_t now = std::time(nullptr);
			std::tm tm{};
#if defined(_WIN32)
			localtime_s(&tm, &now);
#else
			localtime_r(&now, &tm);
#endif
			std::ostringstream oss;
			oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
			return oss.str();
		}

		std::string systemString()
		{
			struct utsname u{};
			if (uname(&u) != 0)
			{
				return "unknown";
			}

			std::ostringstream oss;
			oss << u.sysname << " " << u.release << " " << u.machine;
			return oss.str();
		}

		std::string compileArch()
		{
#if defined(__x86_64__)
			return "x86_64";
#elif defined(__aarch64__) || defined(__arm64__)
			return "arm64";
#else
			return "unknown";
#endif
		}

		std::string buildKind()
		{
#ifdef NDEBUG
			return "Release-like";
#else
			return "Debug-like";
#endif
		}

		template<typename F>
		double timeAvgUs(u64 rounds, F&& fn)
	{
		const auto t0 = Clock::now();
		for (u64 i = 0; i < rounds; ++i)
		{
			fn();
		}
		const auto t1 = Clock::now();
		const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
			return static_cast<double>(us) / static_cast<double>(rounds);
		}

		template<typename F>
		double printLine(Reporter& out, const std::string& name, u64 rounds, F&& fn)
		{
			const double avgUs = timeAvgUs(rounds, std::forward<F>(fn));
			out << "  " << std::left << std::setw(22) << name
				<< " " << std::right << std::setw(10) << std::fixed << std::setprecision(2)
				<< avgUs << " us" << '\n';
			return avgUs;
		}

		void printHeader(Reporter& out, const BenchConfig& cfg)
		{
			out << "obf-mlkem benchmark\n\n";
			out << "Run Time\n";
			out << "  local time            " << nowString() << '\n';
			out << "  system                " << systemString() << '\n';
			out << "  binary arch           " << compileArch() << '\n';
			out << "  build kind            " << buildKind() << '\n';
			out << "  note                  built and run through the x86_64 benchmark path used in this repo" << '\n';
			out << '\n';

			out << "Benchmark Config\n";
			out << "  key search tries      " << cfg.keySearchTries << '\n';
			out << "  cipher warmup tries   " << cfg.cipherWarmupTries << '\n';
			out << "  cipher bench tries    " << cfg.cipherBenchTries << '\n';
			out << "  kem rounds            " << cfg.kemRounds << '\n';
			out << "  codec rounds          " << cfg.codecRounds << '\n';
			out << "  output file           " << cfg.outputPath << '\n';
			out << '\n';
		}

		void printModeInfo(Reporter& out, MlKem::Mode mode, const MlKem& kem, const Kemeleon& codec)
		{
			const auto sz = kem.sizes();
			out << "  mode                  " << modeName(mode) << '\n';
			out << "  raw pk bytes          " << sz.publicKeyBytes << '\n';
			out << "  raw sk bytes          " << sz.secretKeyBytes << '\n';
			out << "  raw ct bytes          " << sz.cipherTextBytes << '\n';
			out << "  code pk bytes         " << codec.codeKeyBytes() << '\n';
			out << "  code ct bytes         " << codec.codeCipherBytes() << '\n';
		}

		void benchMode(Reporter& out, MlKem::Mode mode, const BenchConfig& cfg)
		{
			MlKem kem(mode);
			Kemeleon codec(mode);

			std::vector<u8> keyData;
			MlKem::KeyPair pair;
			for (u64 tries = 0; tries < cfg.keySearchTries; ++tries)
			{
				pair = kem.keyGen();
				if (codec.encodeKey(pair.publicKey, keyData))
			{
				break;
			}
		}
		if (keyData.empty())
		{
			throw std::runtime_error("encodeKey failed during benchmark setup");
		}

		MlKem::EncapResult enc;
		std::vector<u8> cipherData;
		u64 warmupTries = 0;
		for (;;)
		{
			enc = kem.encaps(pair.publicKey);
			if (codec.encodeCipher(enc.cipherText, cipherData))
			{
				break;
			}

			++warmupTries;
			if (warmupTries >= cfg.cipherWarmupTries)
			{
				throw std::runtime_error("encodeCipher failed too many times during benchmark setup");
			}
		}

			out << '\n' << modeName(mode) << '\n';
			printModeInfo(out, mode, kem, codec);
			out << "  warmup success try    " << (warmupTries + 1) << '\n';

			printLine(out, "keyGen", cfg.kemRounds, [&] {
				std::vector<u8> out;
				for (u64 tries = 0; tries < cfg.keySearchTries; ++tries)
				{
					auto x = kem.keyGen();
					if (codec.encodeKey(x.publicKey, out))
				{
					return;
				}
				}
				throw std::runtime_error("keyGen benchmark could not produce an encodable key");
			});

			printLine(out, "encaps", cfg.kemRounds, [&] {
				volatile auto x = kem.encaps(pair.publicKey);
				(void)x;
			});

			printLine(out, "decaps", cfg.kemRounds, [&] {
				volatile auto x = kem.decaps(enc.cipherText, pair.secretKey);
				(void)x;
			});

			printLine(out, "encodeKey", cfg.codecRounds, [&] {
				std::vector<u8> out;
				const bool ok = codec.encodeKey(pair.publicKey, out);
				if (!ok)
			{
					throw std::runtime_error("encodeKey failed during benchmark");
				}
			});

			printLine(out, "decodeKey", cfg.codecRounds, [&] {
				std::vector<u8> out;
				const bool ok = codec.decodeKey(keyData, out);
				if (!ok)
			{
					throw std::runtime_error("decodeKey failed during benchmark");
				}
			});

			printLine(out, "encodeCipher", cfg.codecRounds, [&] {
				std::vector<u8> out;
				for (u64 tries = 0; tries < cfg.cipherBenchTries; ++tries)
				{
					if (codec.encodeCipher(enc.cipherText, out))
					{
					return;
				}
			}
			throw std::runtime_error("encodeCipher failed too many times during benchmark");
			});

			Kemeleon::EncodeCipherStats stats;
			for (u64 i = 0; i < cfg.codecRounds; ++i)
			{
				std::vector<u8> out;
				for (u64 tries = 0; tries < cfg.cipherBenchTries; ++tries)
				{
					if (codec.encodeCipherProfiled(enc.cipherText, out, stats))
					{
						break;
					}
					if (tries + 1 == cfg.cipherBenchTries)
					{
						throw std::runtime_error("encodeCipher profile failed too many times");
					}
				}
			}

			const double ok = static_cast<double>(cfg.codecRounds);
			out
				<< "    tries/success  " << std::fixed << std::setprecision(2) << (static_cast<double>(stats.tries) / ok) << '\n'
				<< "    overflow fails " << (static_cast<double>(stats.overflowFails) / ok) << '\n'
				<< "    zero fails     " << (static_cast<double>(stats.zeroFails) / ok) << '\n'
			<< "    unpack         " << (static_cast<double>(stats.unpackNs) / ok / 1000.0) << " us\n"
			<< "    pick           " << (static_cast<double>(stats.pickNs) / ok / 1000.0) << " us\n"
			<< "    mpz            " << (static_cast<double>(stats.mpzNs) / ok / 1000.0) << " us\n"
				<< "    reject         " << (static_cast<double>(stats.rejectNs) / ok / 1000.0) << " us\n"
				<< "    output         " << (static_cast<double>(stats.outputNs) / ok / 1000.0) << " us\n";

			printLine(out, "decodeCipher", cfg.codecRounds, [&] {
				std::vector<u8> out;
				const bool ok = codec.decodeCipher(cipherData, out);
				if (!ok)
			{
				throw std::runtime_error("decodeCipher failed during benchmark");
			}
		});
	}
}

int main()
{
	try
	{
		const BenchConfig cfg;
		std::ofstream file(cfg.outputPath);
		if (!file)
		{
			throw std::runtime_error("failed to open benchmark output file");
		}

		Reporter out(file);
		printHeader(out, cfg);
		benchMode(out, MlKem::Mode::MlKem512, cfg);
		benchMode(out, MlKem::Mode::MlKem768, cfg);
		benchMode(out, MlKem::Mode::MlKem1024, cfg);
		out << '\n' << "Saved report to " << cfg.outputPath << '\n';
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "benchmark failed: " << e.what() << '\n';
		return 1;
	}
}
