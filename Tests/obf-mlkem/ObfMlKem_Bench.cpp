#include "libPSI/Tools/obf-mlkem/backend/MlKem.h"
#include "libPSI/Tools/obf-mlkem/codec/Kemeleon.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace osuCrypto;

namespace
{
	using Clock = std::chrono::steady_clock;

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
	void printLine(const std::string& name, u64 rounds, F&& fn)
	{
		const double avgUs = timeAvgUs(rounds, std::forward<F>(fn));
		std::cout << "  " << std::left << std::setw(22) << name
			<< " " << std::right << std::setw(10) << std::fixed << std::setprecision(2)
			<< avgUs << " us" << '\n';
	}

	void benchMode(MlKem::Mode mode)
	{
		MlKem kem(mode);
		Kemeleon codec(mode);

		const u64 kemRounds = 200;
		const u64 codecRounds = 200;

		std::vector<u8> keyData;
		MlKem::KeyPair pair;
		for (u64 tries = 0; tries < 128; ++tries)
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

		auto enc = kem.encaps(pair.publicKey);

		std::vector<u8> cipherData;
		u64 warmupTries = 0;
		while (!codec.encodeCipher(enc.cipherText, cipherData))
		{
			++warmupTries;
			if (warmupTries > 32768)
			{
				throw std::runtime_error("encodeCipher failed too many times during benchmark setup");
			}
		}

		std::cout << '\n' << modeName(mode) << '\n';

		printLine("keyGen", kemRounds, [&] {
			std::vector<u8> out;
			for (u64 tries = 0; tries < 128; ++tries)
			{
				auto x = kem.keyGen();
				if (codec.encodeKey(x.publicKey, out))
				{
					return;
				}
			}
			throw std::runtime_error("keyGen benchmark could not produce an encodable key");
		});

		printLine("encaps", kemRounds, [&] {
			volatile auto x = kem.encaps(pair.publicKey);
			(void)x;
		});

		printLine("decaps", kemRounds, [&] {
			volatile auto x = kem.decaps(enc.cipherText, pair.secretKey);
			(void)x;
		});

		printLine("encodeKey", codecRounds, [&] {
			std::vector<u8> out;
			const bool ok = codec.encodeKey(pair.publicKey, out);
			if (!ok)
			{
				throw std::runtime_error("encodeKey failed during benchmark");
			}
		});

		printLine("decodeKey", codecRounds, [&] {
			std::vector<u8> out;
			const bool ok = codec.decodeKey(keyData, out);
			if (!ok)
			{
				throw std::runtime_error("decodeKey failed during benchmark");
			}
		});

		printLine("encodeCipher", codecRounds, [&] {
			std::vector<u8> out;
			for (u64 tries = 0; tries < 4096; ++tries)
			{
				if (codec.encodeCipher(enc.cipherText, out))
				{
					return;
				}
			}
			throw std::runtime_error("encodeCipher failed too many times during benchmark");
		});

		Kemeleon::EncodeCipherStats stats;
		for (u64 i = 0; i < codecRounds; ++i)
		{
			std::vector<u8> out;
			for (u64 tries = 0; tries < 4096; ++tries)
			{
				if (codec.encodeCipherProfiled(enc.cipherText, out, stats))
				{
					break;
				}
				if (tries + 1 == 4096)
				{
					throw std::runtime_error("encodeCipher profile failed too many times");
				}
			}
		}

		const double ok = static_cast<double>(codecRounds);
		std::cout
			<< "    tries/success  " << std::fixed << std::setprecision(2) << (static_cast<double>(stats.tries) / ok) << '\n'
			<< "    overflow fails " << (static_cast<double>(stats.overflowFails) / ok) << '\n'
			<< "    zero fails     " << (static_cast<double>(stats.zeroFails) / ok) << '\n'
			<< "    unpack         " << (static_cast<double>(stats.unpackNs) / ok / 1000.0) << " us\n"
			<< "    pick           " << (static_cast<double>(stats.pickNs) / ok / 1000.0) << " us\n"
			<< "    mpz            " << (static_cast<double>(stats.mpzNs) / ok / 1000.0) << " us\n"
			<< "    reject         " << (static_cast<double>(stats.rejectNs) / ok / 1000.0) << " us\n"
			<< "    output         " << (static_cast<double>(stats.outputNs) / ok / 1000.0) << " us\n";

		printLine("decodeCipher", codecRounds, [&] {
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
		std::cout << "obf-mlkem benchmark\n";
		benchMode(MlKem::Mode::MlKem512);
		benchMode(MlKem::Mode::MlKem768);
		benchMode(MlKem::Mode::MlKem1024);
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "benchmark failed: " << e.what() << '\n';
		return 1;
	}
}
