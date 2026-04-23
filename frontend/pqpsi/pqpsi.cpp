#include "pqpsi.h"
#include "Crypto/PRNG.h"
#include "Common/Defines.h"
#include "Common/Log.h"
#include "Common/Log1.h"
#include <set>
#include "okvs/okvs.h"
#include <fstream>
#include "util.h"
#include "permutation.h"
#include "obf-mlkem/backend/MlKem.h"
#include "obf-mlkem/codec/Kemeleon.h"
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <atomic>
#include <chrono>
#include <thread>


using namespace osuCrypto;

namespace
{
	// obf-mlkem mode pinned to 1024
	constexpr MlKem::Mode kMode = MlKem::Mode::MlKem512;
	constexpr u64 kKemBytes = KEM_key_block_size * sizeof(block);
	constexpr u64 kSeedBytes = MlKem::KeyGenSeedSize;
	constexpr u64 kEncodeKeyMaxTries = 64;

	// row <-> byte 
	std::array<u8, kKemBytes> keyToBytes(const kemKey& in)
	{
		std::array<u8, kKemBytes> out{};
		std::memcpy(out.data(), in.data(), kKemBytes);
		return out;
	}

	std::array<u8, kKemBytes> blocksToBytes(const std::vector<block>& in)
	{
		if (in.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("blocksToBytes: wrong block count");
		}

		std::array<u8, kKemBytes> out{};
		std::memcpy(out.data(), in.data(), kKemBytes);
		return out;
	}

	void bytesToKey(span<const u8> src, kemKey& out)
	{
		if (src.size() != kKemBytes)
		{
			throw std::invalid_argument("bytesToKey: wrong byte count");
		}

		std::memcpy(out.data(), src.data(), kKemBytes);
	}

	//// block row -> bit vector
	void blocksToBits(const block* in, Bits& out)
	{
		out.resize(KEM_key_size_bit);
		const u8* bytes = reinterpret_cast<const u8*>(in);
		for (u64 i = 0; i < kKemBytes; ++i)
		{
			const u8 b = bytes[i];
			const u64 o = i * 8;
			out[o + 0] = static_cast<u8>((b >> 0) & 1U);
			out[o + 1] = static_cast<u8>((b >> 1) & 1U);
			out[o + 2] = static_cast<u8>((b >> 2) & 1U);
			out[o + 3] = static_cast<u8>((b >> 3) & 1U);
			out[o + 4] = static_cast<u8>((b >> 4) & 1U);
			out[o + 5] = static_cast<u8>((b >> 5) & 1U);
			out[o + 6] = static_cast<u8>((b >> 6) & 1U);
			out[o + 7] = static_cast<u8>((b >> 7) & 1U);
		}
	}

	//// (k, v) -> (k, H_expand(k) ⊕ Π(v))
	std::vector<u8> hashExpandKeyBytes(const block& key, u64 outBytes)
	{
		std::vector<u8> out(outBytes);
		AES h(key);

		u64 off = 0;
		u64 ctr = 1;
		while (off < outBytes)
		{
			const block in = toBlock(0, ctr);
			const block y = h.ecbEncBlock(in);
			const u64 take = std::min<u64>(sizeof(block), outBytes - off);
			std::memcpy(out.data() + off, &y, take);
			off += take;
			++ctr;
		}

		return out;
	}

	void xorHashedKeyIntoRow(const block& key, std::vector<block>& row)
	{
		if (row.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("xorHashedKeyIntoRow: wrong row block count");
		}

		const u64 rowBytes = static_cast<u64>(row.size() * sizeof(block));
		auto h = hashExpandKeyBytes(key, rowBytes);
		u8* rowPtr = reinterpret_cast<u8*>(row.data());
		for (u64 i = 0; i < rowBytes; ++i)
		{
			rowPtr[i] ^= h[i];
		}
	}

	//// bit vector -> block row
	void bitsToBlocks(const Bits& in, block* out)
	{
		if (in.size() != KEM_key_size_bit)
		{
			throw std::invalid_argument("bitsToBlocks: wrong bit count");
		}

		u8* bytes = reinterpret_cast<u8*>(out);
		for (u64 i = 0; i < kKemBytes; ++i)
		{
			const u64 o = i * 8;
			bytes[i] =
				static_cast<u8>((in[o + 0] & 1U) << 0) |
				static_cast<u8>((in[o + 1] & 1U) << 1) |
				static_cast<u8>((in[o + 2] & 1U) << 2) |
				static_cast<u8>((in[o + 3] & 1U) << 3) |
				static_cast<u8>((in[o + 4] & 1U) << 4) |
				static_cast<u8>((in[o + 5] & 1U) << 5) |
				static_cast<u8>((in[o + 6] & 1U) << 6) |
				static_cast<u8>((in[o + 7] & 1U) << 7);
		}
	}

	//// one kem key row through permutation
	void encryptKemKeyToRow(const ConstructionPermutation& P, const kemKey& in, std::vector<block>& outRow, Bits& bitBuf)
	{
		blocksToBits(in.data(), bitBuf);
		bitBuf = P.encrypt(std::move(bitBuf));
		outRow.resize(KEM_key_block_size);
		bitsToBlocks(bitBuf, outRow.data());
	}

	//// one row back from permutation domain
	void decryptKemRow(const ConstructionPermutation& P, std::vector<block>& row, Bits& bitBuf)
	{
		if (row.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("decryptKemRow: wrong block count");
		}

		blocksToBits(row.data(), bitBuf);
		bitBuf = P.decrypt(std::move(bitBuf));
		bitsToBlocks(bitBuf, row.data());
	}

	//// split [0,n) into contiguous ranges
	template<typename Fn>
	void parallelForRange(size_t n, size_t minWorkPerThread, Fn&& fn)
	{
		if (n == 0)
		{
			return;
		}

		const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
		const size_t maxThreadsByWork = std::max<size_t>(1, n / std::max<size_t>(1, minWorkPerThread));
		const size_t threads = std::max<size_t>(1, std::min(hw, maxThreadsByWork));
		if (threads <= 1)
		{
			fn(0, n);
			return;
		}

		const size_t chunk = (n + threads - 1) / threads;
		std::vector<std::thread> workers;
		workers.reserve(threads);

		for (size_t t = 0; t < threads; ++t)
		{
			const size_t begin = t * chunk;
			if (begin >= n)
			{
				break;
			}
			const size_t end = std::min(n, begin + chunk);
			workers.emplace_back([&, begin, end]()
			{
				fn(begin, end);
			});
		}

		for (auto& th : workers)
		{
			th.join();
		}
	}
}


void KemKeyGeneration(std::vector<kemKey>& sk, std::vector<kemKey>& pk)
{
	//// obf-mlkem keygen + obf public-key encode
	MlKem kem(kMode);
	Kemeleon codec(kMode);

	for (size_t i = 0; i < sk.size(); i++) //TODO: get the real keys from KEM
	{
		//// set current sk/pk row to zero first, so no leftover bytes leak in
		for (size_t j = 0; j < sk[i].size(); j++)
		{
			sk[i][j] = ZeroBlock;
			pk[i][j] = ZeroBlock;
		}

		std::array<u8, MlKem::KeyGenSeedSize> seed{};
		std::vector<u8> codePk;
		bool ok = false;
		//// retry encodeKey with varied deterministic seeds, max 64 times
		for (u64 t = 0; t < kEncodeKeyMaxTries; ++t)
		{
			for (u64 j = 0; j < seed.size(); ++j)
			{
				//// per-index + per-try seed schedule
				seed[j] = static_cast<u8>(((i + 1) * 131 + (j + 3) * 17 + t * 29) & 0xFF);
			}

			auto keyPair = kem.keyGen(seed);
			if (codec.encodeKey(keyPair.publicKey, codePk))
			{
				ok = true;
				break;
			}
		}

		if (!ok)
		{
			throw std::runtime_error("KemKeyGeneration: encodeKey failed after retries");
		}

		//// sk slot stores keygen seed
		auto skBytes = keyToBytes(sk[i]);
		std::copy(seed.begin(), seed.end(), skBytes.begin());
		bytesToKey(skBytes, sk[i]);

		//// pk slot stores encoded public key
		auto pkBytes = keyToBytes(pk[i]);
		if (codePk.size() > pkBytes.size())
		{
			throw std::runtime_error("KemKeyGeneration: encoded pk too large");
		}
		std::copy(codePk.begin(), codePk.end(), pkBytes.begin());
		bytesToKey(pkBytes, pk[i]);
	}
}
void Encaps(std::vector<std::vector<block>>& input, std::vector<kemKey>& out)
{
	//// decode obf public key then encaps
	MlKem kem(kMode);
	Kemeleon codec(kMode);
	const auto sizes = kem.sizes();

	out.resize(input.size());

	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i].size() != KEM_key_block_size) {
			throw std::invalid_argument("Encaps: wrong inner vector size");
		}

		auto rowBytes = blocksToBytes(input[i]);
		std::vector<u8> rawPk;
		if (!codec.decodeKey(span<const u8>(rowBytes.data(), codec.codeKeyBytes()), rawPk))
		{
			//// strict mode fail on bad encoded key row
			std::ostringstream oss;
			oss << "Encaps: decodeKey failed at row " << i
				<< ", expected encoded pk bytes " << codec.codeKeyBytes();
			throw std::runtime_error(oss.str());
		}

		auto enc = kem.encaps(rawPk);
		auto outBytes = keyToBytes(out[i]);
		std::fill(outBytes.begin(), outBytes.end(), 0);

		//// out format ct || shared-secret-tag
		if (sizes.cipherTextBytes + MlKem::SharedSecretSize > outBytes.size())
		{
			throw std::runtime_error("Encaps: output row too small");
		}

		std::copy(enc.cipherText.begin(), enc.cipherText.end(), outBytes.begin());
		std::copy(enc.sharedSecret.begin(), enc.sharedSecret.end(), outBytes.begin() + sizes.cipherTextBytes);
		bytesToKey(outBytes, out[i]);
	}
	//TODO: write Encaps here

}
bool Decaps(kemKey sk, std::vector<block> value)
{
	//// restore secret from seed then verify shared-secret-tag
	MlKem kem(kMode);
	const auto sizes = kem.sizes();
	if (value.size() != KEM_key_block_size)
	{
		return false;
	}

	auto skBytes = keyToBytes(sk);
	std::array<u8, MlKem::KeyGenSeedSize> seed{};
	std::copy(skBytes.begin(), skBytes.begin() + kSeedBytes, seed.begin());
	auto keyPair = kem.keyGen(seed);

	auto valueBytes = blocksToBytes(value);
	if (sizes.cipherTextBytes + MlKem::SharedSecretSize > valueBytes.size())
	{
		return false;
	}

	std::vector<u8> ct(sizes.cipherTextBytes);
	std::copy(valueBytes.begin(), valueBytes.begin() + sizes.cipherTextBytes, ct.begin());
	auto got = kem.decaps(ct, keyPair.secretKey);

	std::array<u8, MlKem::SharedSecretSize> tag{};
	std::copy(
		valueBytes.begin() + sizes.cipherTextBytes,
		valueBytes.begin() + sizes.cipherTextBytes + MlKem::SharedSecretSize,
		tag.begin());

	bool ok = std::equal(got.begin(), got.end(), tag.begin());

	//TODO write the decaps
	return ok;
}



void pqpsi(
	u64 myIdx,
	u64 setSize,
	std::vector<block> inputSet,
	u64 type_okvs = SimulatedOkvs,
	u64* intersectionCountOut = nullptr,
	PqPsiStageMs* stageOut = nullptr)
{	
	using Clock = std::chrono::steady_clock;
	auto asMs = [](const Clock::time_point& a, const Clock::time_point& b)
	{
		return std::chrono::duration<double, std::milli>(b - a).count();
	};
	PqPsiStageMs localStage{};
	const auto allStart = Clock::now();

	//whether print trace
	const bool traceOn = []() {
		const char* v = std::getenv("PQPSI_TRACE");
		return v && *v && *v != '0';
	}();

	auto trace = [&](const char* msg)
	{
		if (!traceOn) return;
		std::cerr << "[pqpsi][p" << myIdx << "] " << msg << std::endl;
	};

	auto parseEnvDouble = [](const char* key, double fallback)
	{
		const char* v = std::getenv(key);
		if (!v || !*v)
		{
			return fallback;
		}
		try
		{
			return std::stod(v);
		}
		catch (...)
		{
			return fallback;
		}
	};

	struct SimNetConfig
	{
		double delayMs = 0.0;
		double bandwidthMBps = 0.0;
		bool enabled() const
		{
			return delayMs > 0.0 || bandwidthMBps > 0.0;
		}
		double sendCostMs(u64 bytes) const
		{
			double ms = std::max(0.0, delayMs);
			if (bandwidthMBps > 0.0)
			{
				const double bytesPerSec = bandwidthMBps * 1024.0 * 1024.0;
				ms += (static_cast<double>(bytes) / bytesPerSec) * 1000.0;
			}
			return ms;
		}
	};

	const SimNetConfig simNet{
		parseEnvDouble("PQPSI_SIM_NET_DELAY_MS", 0.0),
		parseEnvDouble("PQPSI_SIM_NET_BW_MBPS", 0.0)
	};

	auto maybeSimulateSend = [&](u64 bytes)
	{
		if (!simNet.enabled())
		{
			return;
		}
		const double ms = simNet.sendCostMs(bytes);
		if (ms > 0.0)
		{
			std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
		}
	};

	u64  psiSecParam = 40, bitSize = 128, numThreads = 1, nParties = 2;
	PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987045));

	PRNG prng1(_mm_set_epi32(4253465, myIdx, myIdx, myIdx)); //for test
	if (myIdx == 0)
	{
		if (inputSet.size() > 2)
			inputSet[2] = prng1.get<block>();
	}


	std::string name("psi");
	if (const char* runTag = std::getenv("PQPSI_RUN_TAG"))
	{
		if (*runTag)
		{
			name += "_";
			name += runTag;
		}
	}
	BtIOService ios(0);
	u32 portBase = 1210;
	if (const char* v = std::getenv("PQPSI_PORT_BASE"))
	{
		try
		{
			portBase = static_cast<u32>(std::stoul(v));
		}
		catch (...) {}
	}

	std::vector<BtEndpoint> ep(nParties);

	u64 offlineTimeTot(0);
	u64 onlineTimeTot(0);
	Timer timer;

	for (u64 i = 0; i < nParties; ++i)
	{
		if (i < myIdx)
		{
			u32 port = portBase + i * 10 + myIdx;//get the same port; i=1 & pIdx=2 =>port=102
			ep[i].start(ios, "localhost", port, false, name); //channel bwt i and pIdx, where i is sender
		}
		else if (i > myIdx)
		{
			u32 port = portBase + myIdx * 10 + i;//get the same port; i=2 & pIdx=1 =>port=102
			ep[i].start(ios, "localhost", port, true, name); //channel bwt i and pIdx, where i is receiver
		}
	}


	std::vector<std::vector<Channel*>> chls(nParties);

	for (u64 i = 0; i < nParties; ++i)
	{
		if (i != myIdx) {
			chls[i].resize(numThreads);
			for (u64 j = 0; j < numThreads; ++j)
			{
				const std::string chName = name + "_ch" + std::to_string(j);
				chls[i][j] = &ep[i].addChannel(chName, chName);
			}
		}
	}


	std::vector<std::thread>  pThrds(nParties);
	std::vector<u32> mIntersection;

	
	auto start = timer.setTimePoint("start");

	//##########################
	//### ML-KEM key generations
	//##########################

	std::vector<kemKey> recv_sk(inputSet.size());
	std::vector<kemKey> recv_pk(inputSet.size());
	std::vector<kemKey> sender_pk(inputSet.size());

	const auto tKemKeyGen0 = Clock::now();
	KemKeyGeneration(recv_sk, recv_pk);
	const auto tKemKeyGen1 = Clock::now();
	localStage.kemKeyGenMs += asMs(tKemKeyGen0, tKemKeyGen1);
	trace("KemKeyGeneration done");
	
	
	//##########################
	//### OKVS Encoding
	//##########################

	u64 okvsTableSize = 0; //depending on okvs variant
	if (type_okvs == RandomBandOkvs)
		okvsTableSize = RbOkvsTableSize(inputSet.size());
	else
		okvsTableSize = okvsLengthScale * inputSet.size();
	u64 rowOkvsBlkSize = KEM_key_block_size;
	int s = 1400; // better round count, still inside lambda bound
	ConstructionPermutation P(Keccak_size_bit, KEM_key_size_bit, s, Keccak1600Adapter::pi, Keccak1600Adapter::pi_inv);


	if (myIdx == 0) //receiver
	{
		trace("receiver branch start");
		std::vector<std::vector<block>> setValues(recv_pk.size());

		const auto tPermutePk0 = Clock::now();
		parallelForRange(setValues.size(), 16, [&](size_t begin, size_t end)
		{
			Bits encBuf;
			encBuf.reserve(KEM_key_size_bit);
			for (size_t i = begin; i < end; ++i)
			{
				encryptKemKeyToRow(P, recv_pk[i], setValues[i], encBuf);
				xorHashedKeyIntoRow(inputSet[i], setValues[i]);
			}
		});
		const auto tPermutePk1 = Clock::now();
		localStage.permuteMs += asMs(tPermutePk0, tPermutePk1);

		std::vector<std::vector<block>> okvsTable(okvsTableSize);

		const auto tOkvsEnc0 = Clock::now();
		if (type_okvs == RandomBandOkvs)
			RandomBandOkvsEncode(inputSet, setValues, okvsTable);
		else
			SimulatedOkvsEncode(inputSet, setValues, okvsTable);
		const auto tOkvsEnc1 = Clock::now();
		localStage.okvsEncodeMs += asMs(tOkvsEnc0, tOkvsEnc1);
		trace("receiver okvs1 encode done");

		 // send okvs
		std::vector<block> flat(okvsTableSize * rowOkvsBlkSize);

		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy(flat.data() + j * rowOkvsBlkSize, okvsTable[j].data(), rowOkvsBlkSize * sizeof(block));
		}


		trace("receiver send okvs1 start");
		const auto tSend1_0 = Clock::now();
		const u64 sendBytes1 = static_cast<u64>(flat.size() * sizeof(block));
		maybeSimulateSend(sendBytes1);
		chls[1][0]->send(flat.data(), sendBytes1);
		const auto tSend1_1 = Clock::now();
		localStage.networkSendMs += asMs(tSend1_0, tSend1_1);
		localStage.networkSendBytes += static_cast<double>(sendBytes1);
		trace("receiver send okvs1 done");
		

		/////receving the second OKVS

		std::vector<std::vector<block>> okvsTable2(okvsTableSize,std::vector<block>(rowOkvsBlkSize));
		std::vector<std::vector<block>> setValues2(inputSet.size());

		trace("receiver recv okvs2 start");
		const auto tRecv2_0 = Clock::now();
		const u64 recvBytes2 = static_cast<u64>(flat.size() * sizeof(block));
		chls[1][0]->recv(flat.data(), recvBytes2);
		const auto tRecv2_1 = Clock::now();
		localStage.networkRecvMs += asMs(tRecv2_0, tRecv2_1);
		localStage.networkRecvBytes += static_cast<double>(recvBytes2);
		trace("receiver recv okvs2 done");
		
		for (size_t j = 0; j < okvsTableSize; ++j) {
			// std::memcpy(okvsTable[j].data(), flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
			std::memcpy(okvsTable2[j].data(), flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
		}
		

		const auto tOkvsDec2_0 = Clock::now();
		if (type_okvs == RandomBandOkvs)
			RandomBandOkvsDecode(okvsTable2, inputSet, setValues2);
		else
			SimulatedOkvsDecode(okvsTable2, inputSet, setValues2);
		const auto tOkvsDec2_1 = Clock::now();
		localStage.okvsDecodeMs += asMs(tOkvsDec2_0, tOkvsDec2_1);
		trace("receiver okvs2 decode done");
		//// decode output in permutation domain, map back before decaps
		const auto tPermDec2_0 = Clock::now();
		parallelForRange(setValues2.size(), 16, [&](size_t begin, size_t end)
		{
			Bits decBuf;
			decBuf.reserve(KEM_key_size_bit);
			for (size_t i = begin; i < end; ++i)
			{
				//// Π(v) = (H(k) ⊕ Π(v)) ⊕ H(k)
				xorHashedKeyIntoRow(inputSet[i], setValues2[i]);
				decryptKemRow(P, setValues2[i], decBuf);
			}
		});
		const auto tPermDec2_1 = Clock::now();
		localStage.permDecryptMs += asMs(tPermDec2_0, tPermDec2_1);
		trace("receiver kem decrypt done");
		const auto tDecaps0 = Clock::now();
		for (u32 i = 0; i < inputSet.size(); i++)
		{
			if (Decaps(recv_sk[i], setValues2[i]))
				mIntersection.push_back(i);
		}
		const auto tDecaps1 = Clock::now();
		localStage.kemCoreMs += asMs(tDecaps0, tDecaps1);
		trace("receiver decaps done");
		if (intersectionCountOut)
			*intersectionCountOut = static_cast<u64>(mIntersection.size());


	}
	else if (myIdx == 1) //sender
	{
		trace("sender branch start");
		std::vector<block> recv_flat(okvsTableSize * rowOkvsBlkSize);
		trace("sender recv okvs1 start");
		const auto tRecv1_0 = Clock::now();
		const u64 recvBytes1 = static_cast<u64>(recv_flat.size() * sizeof(block));
		chls[0][0]->recv(recv_flat.data(), recvBytes1);
		const auto tRecv1_1 = Clock::now();
		localStage.networkRecvMs += asMs(tRecv1_0, tRecv1_1);
		localStage.networkRecvBytes += static_cast<double>(recvBytes1);
		trace("sender recv okvs1 done");

		std::vector<std::vector<block>> okvsTable(okvsTableSize,
		std::vector<block>(rowOkvsBlkSize));

		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy(okvsTable[j].data(), recv_flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
		}

		std::vector<std::vector<block>> setValues(inputSet.size());

		const auto tOkvsDec1_0 = Clock::now();
		if (type_okvs == RandomBandOkvs)
			RandomBandOkvsDecode(okvsTable, inputSet, setValues);
		else
			SimulatedOkvsDecode(okvsTable,inputSet, setValues);
		const auto tOkvsDec1_1 = Clock::now();
		localStage.okvsDecodeMs += asMs(tOkvsDec1_0, tOkvsDec1_1);
		trace("sender okvs1 decode done");
		//// decode output in permutation domain, map back before encaps
		const auto tPermDec1_0 = Clock::now();
		parallelForRange(setValues.size(), 16, [&](size_t begin, size_t end)
		{
			Bits decBuf;
			decBuf.reserve(KEM_key_size_bit);
			for (size_t i = begin; i < end; ++i)
			{
				//// Π(v) = (H(k) ⊕ Π(v)) ⊕ H(k)
				xorHashedKeyIntoRow(inputSet[i], setValues[i]);
				decryptKemRow(P, setValues[i], decBuf);
			}
		});
		const auto tPermDec1_1 = Clock::now();
		localStage.permDecryptMs += asMs(tPermDec1_0, tPermDec1_1);
		trace("sender kem decrypt done");

		const auto tEncaps0 = Clock::now();
		Encaps(setValues, sender_pk);
		const auto tEncaps1 = Clock::now();
		localStage.kemCoreMs += asMs(tEncaps0, tEncaps1);
		trace("sender encaps done");

		std::vector<std::vector<block>> setValues2(sender_pk.size()); //the setValue for the next OKVS

		const auto tPermuteCt0 = Clock::now();
		parallelForRange(setValues2.size(), 16, [&](size_t begin, size_t end)
		{
			Bits encBuf;
			encBuf.reserve(KEM_key_size_bit);
			for (size_t i = begin; i < end; ++i)
			{
				encryptKemKeyToRow(P, sender_pk[i], setValues2[i], encBuf);
				xorHashedKeyIntoRow(inputSet[i], setValues2[i]);
			}
		});
		const auto tPermuteCt1 = Clock::now();
		localStage.permuteMs += asMs(tPermuteCt0, tPermuteCt1);

		std::vector<std::vector<block>> okvsTable2(okvsTableSize); 

		const auto tOkvsEnc2_0 = Clock::now();
		if (type_okvs == RandomBandOkvs)
			RandomBandOkvsEncode(inputSet, setValues2, okvsTable2);
		else
			SimulatedOkvsEncode(inputSet, setValues2, okvsTable2);
		const auto tOkvsEnc2_1 = Clock::now();
		localStage.okvsEncodeMs += asMs(tOkvsEnc2_0, tOkvsEnc2_1);
		trace("sender okvs2 encode done");

		//copy and send the okvs to the sender
		for (size_t j = 0; j < okvsTableSize; ++j) {
			// std::memcpy(recv_flat.data() + j * rowOkvsBlkSize, okvsTable[j].data(), rowOkvsBlkSize * sizeof(block));
			std::memcpy(recv_flat.data() + j * rowOkvsBlkSize, okvsTable2[j].data(), rowOkvsBlkSize * sizeof(block));
		}
		trace("sender send okvs2 start");
		const auto tSend2_0 = Clock::now();
		const u64 sendBytes2 = static_cast<u64>(recv_flat.size() * sizeof(block));
		maybeSimulateSend(sendBytes2);
		chls[0][0]->send(recv_flat.data(), sendBytes2);
		const auto tSend2_1 = Clock::now();
		localStage.networkSendMs += asMs(tSend2_0, tSend2_1);
		localStage.networkSendBytes += static_cast<double>(sendBytes2);
		trace("sender send okvs2 done");

	}


	auto initDone = timer.setTimePoint("initDone");



	for (u64 i = 0; i < nParties; ++i)
	{
		if (i != myIdx)
		{
			for (u64 j = 0; j < numThreads; ++j)
			{
				chls[i][j]->close();
			}
		}
	}

	for (u64 i = 0; i < nParties; ++i)
	{
		if (i != myIdx)
			ep[i].stop();
	}


	ios.stop();
	localStage.totalMs = asMs(allStart, Clock::now());
	if (stageOut)
	{
		*stageOut = localStage;
	}
	trace("pqpsi done");
}



void PqPsi_Test_Main()
{
	u64 setSize = 1 << 2, psiSecParam = 40, bitSize = 128;
	PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
	std::vector<block>mSet(setSize);
	for (u64 i = 0; i < setSize; ++i)
	{
		mSet[i] = prng.get<block>();
	}
	std::vector<std::thread>  pThrds(2);
	for (u64 pIdx = 0; pIdx < pThrds.size(); ++pIdx)
	{
		pThrds[pIdx] = std::thread([&, pIdx]() {
			//	Channel_party_test(pIdx);
			pqpsi(pIdx, setSize, mSet, SimulatedOkvs);
			});
	}
	for (u64 pIdx = 0; pIdx < pThrds.size(); ++pIdx)
		pThrds[pIdx].join();


}

void PqPsi_RbOkvs_Test_Main()
{
	u64 got = 0, expected = 0;
	(void)PqPsi_RbOkvs_Test_Check(got, expected);
}

bool PqPsi_RbOkvs_Test_Check(u64& gotIntersection, u64& expectedIntersection)
{
	return PqPsi_RbOkvs_RunCheck(1 << 2, gotIntersection, expectedIntersection);
}

bool PqPsi_RbOkvs_RunCheck(
	u64 setSize,
	u64& gotIntersection,
	u64& expectedIntersection,
	PqPsiRunProfile* profileOut)
{
	if (setSize == 0)
	{
		gotIntersection = 0;
		expectedIntersection = 0;
		return true;
	}

	expectedIntersection = (setSize > 2) ? (setSize - 1) : setSize;
	gotIntersection = 0;

	PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
	std::vector<block> mSet(setSize);
	for (u64 i = 0; i < setSize; ++i)
	{
		mSet[i] = prng.get<block>();
	}

	std::atomic<u64> recvIntersection{ 0 };
	PqPsiStageMs stage0{};
	PqPsiStageMs stage1{};
	std::vector<std::thread> pThrds(2);
	for (u64 pIdx = 0; pIdx < pThrds.size(); ++pIdx)
	{
		pThrds[pIdx] = std::thread([&, pIdx]() {
			if (pIdx == 0)
			{
				u64 localCount = 0;
				pqpsi(pIdx, setSize, mSet, RandomBandOkvs, &localCount, &stage0);
				recvIntersection.store(localCount, std::memory_order_relaxed);
			}
			else
			{
				pqpsi(pIdx, setSize, mSet, RandomBandOkvs, nullptr, &stage1);
			}
			});
	}
	for (u64 pIdx = 0; pIdx < pThrds.size(); ++pIdx)
		pThrds[pIdx].join();

	gotIntersection = recvIntersection.load(std::memory_order_relaxed);
	if (profileOut)
	{
		profileOut->party0 = stage0;
		profileOut->party1 = stage1;
	}
	return gotIntersection == expectedIntersection;
}
