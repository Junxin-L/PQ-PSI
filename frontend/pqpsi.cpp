#include "pqpsi.h"
#include "Crypto/PRNG.h"
#include "Common/Defines.h"
#include "Common/Log.h"
#include "Common/Log1.h"
#include <set>
#include "okvs.h"
#include <fstream>
#include "util.h"
#include <permutation.h>
#include "obf-mlkem/backend/MlKem.h"
#include "obf-mlkem/codec/Kemeleon.h"
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <sstream>


using namespace osuCrypto;

namespace
{
	//// obf-mlkem mode pinned to 1024
	constexpr MlKem::Mode kMode = MlKem::Mode::MlKem1024;
	constexpr u64 kKemBytes = KEM_key_block_size * sizeof(block);
	constexpr u64 kSeedBytes = MlKem::KeyGenSeedSize;
	constexpr u64 kEncodeKeyMaxTries = 64;

	//// row <-> byte view helper
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

	//// inverse permutation on one kem row
	void decryptKemRow(const ConstructionPermutation& P, std::vector<block>& row)
	{
		if (row.size() != KEM_key_block_size)
		{
			throw std::invalid_argument("decryptKemRow: wrong block count");
		}

		kemKey tmp{};
		std::memcpy(tmp.data(), row.data(), kKemBytes);
		Bits Y = P.decrypt(KemKeyToBits(tmp));
		auto dec = BitsToKemKey(Y);
		std::memcpy(row.data(), dec.data(), kKemBytes);
	}
}


void KemKeyGeneration(std::vector<kemKey>& sk, std::vector<kemKey>& pk)
{
	//// obf-mlkem keygen + obf public-key encode
	MlKem kem(kMode);
	Kemeleon codec(kMode);

	for (size_t i = 0; i < sk.size(); i++) //TODO: get the real keys from KEM
	{
		//// clear storage slot
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



void pqpsi(u64 myIdx, u64 setSize, std::vector<block> inputSet)
{
	const bool traceOn = []() {
		const char* v = std::getenv("PQPSI_TRACE");
		return v && *v && *v != '0';
	}();

	auto trace = [&](const char* msg)
	{
		if (!traceOn) return;
		std::cerr << "[pqpsi][p" << myIdx << "] " << msg << std::endl;
	};

	u64  psiSecParam = 40, bitSize = 128, numThreads = 1, nParties = 2;
	PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987045));

	PRNG prng1(_mm_set_epi32(4253465, myIdx, myIdx, myIdx)); //for test
	if (myIdx == 0)
		inputSet[2] = prng1.get<block>();;


	std::string name("psi");
	BtIOService ios(0);

	std::vector<BtEndpoint> ep(nParties);

	u64 offlineTimeTot(0);
	u64 onlineTimeTot(0);
	Timer timer;

	for (u64 i = 0; i < nParties; ++i)
	{
		if (i < myIdx)
		{
			u32 port = 1210 + i * 10 + myIdx;//get the same port; i=1 & pIdx=2 =>port=102
			ep[i].start(ios, "localhost", port, false, name); //channel bwt i and pIdx, where i is sender
		}
		else if (i > myIdx)
		{
			u32 port = 1210 + myIdx * 10 + i;//get the same port; i=2 & pIdx=1 =>port=102
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
				//chls[i][j] = &ep[i].addChannel("chl" + std::to_string(j), "chl" + std::to_string(j));
				chls[i][j] = &ep[i].addChannel(name, name);
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

	KemKeyGeneration(recv_sk, recv_pk);
	trace("KemKeyGeneration done");
	
	
	//##########################
	//### OKVS Encoding
	//##########################

	u64 okvsTableSize= okvsLengthScale * inputSet.size(); //depending on okvs variant
	u64 rowOkvsBlkSize = KEM_key_block_size;
	int s = Keccak_size_bit; //permuation parameter -- need to update if needed
	ConstructionPermutation P(Keccak_size_bit, KEM_key_size_bit, s, Keccak1600Adapter::pi, Keccak1600Adapter::pi_inv);


	if (myIdx == 0) //receiver
	{
		trace("receiver branch start");
		std::vector<std::vector<block>> setValues;
		setValues.reserve(recv_pk.size());


		for (size_t i = 0; i < recv_pk.size(); i++)
		{
			Bits Y = P.encrypt(KemKeyToBits(recv_pk[i])); //permuation
			std::array<block, KEM_key_block_size> temp = BitsToKemKey(Y);
			setValues.emplace_back(temp.begin(), temp.end()); 

			//TODO: need to xor with H(key) -- currently, the size is different
		}

		std::vector<std::vector<block>> okvsTable(okvsTableSize);

		SimulatedOkvsEncode(inputSet, setValues, okvsTable);
		trace("receiver okvs1 encode done");

		 // send okvs
		std::vector<block> flat(okvsTableSize * rowOkvsBlkSize);

		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy(flat.data() + j * rowOkvsBlkSize, okvsTable[j].data(), rowOkvsBlkSize * sizeof(block));
		}


		trace("receiver send okvs1 start");
		chls[1][0]->send(flat.data(), flat.size() * sizeof(block));
		trace("receiver send okvs1 done");
		

		/////receving the second OKVS

		std::vector<std::vector<block>> okvsTable2(okvsTableSize,std::vector<block>(rowOkvsBlkSize));
		std::vector<std::vector<block>> setValues2(inputSet.size());

		trace("receiver recv okvs2 start");
		chls[1][0]->recv(flat.data(), flat.size() * sizeof(block));
		trace("receiver recv okvs2 done");
		
		for (size_t j = 0; j < okvsTableSize; ++j) {
			// std::memcpy(okvsTable[j].data(), flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
			std::memcpy(okvsTable2[j].data(), flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
		}
		

		SimulatedOkvsDecode(okvsTable2, inputSet, setValues2);
		trace("receiver okvs2 decode done");
		//// decode output in permutation domain, map back before decaps
		for (u64 i = 0; i < setValues2.size(); ++i)
		{
			decryptKemRow(P, setValues2[i]);
		}
		trace("receiver kem decrypt done");
		for (u32 i = 0; i < inputSet.size(); i++)
		{
			if (Decaps(recv_sk[i], setValues2[i]))
				mIntersection.push_back(i);
		}
		trace("receiver decaps done");


	}
	else if (myIdx == 1) //sender
	{
		trace("sender branch start");
		std::vector<block> recv_flat(okvsTableSize * rowOkvsBlkSize);
		trace("sender recv okvs1 start");
		chls[0][0]->recv(recv_flat.data(), recv_flat.size() * sizeof(block));
		trace("sender recv okvs1 done");

		std::vector<std::vector<block>> okvsTable(okvsTableSize,
		std::vector<block>(rowOkvsBlkSize));

		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy(okvsTable[j].data(), recv_flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
		}

		std::vector<std::vector<block>> setValues(inputSet.size());

		
	    SimulatedOkvsDecode(okvsTable,inputSet, setValues);
		trace("sender okvs1 decode done");
		//// decode output in permutation domain, map back before encaps
		for (u64 i = 0; i < setValues.size(); ++i)
		{
			decryptKemRow(P, setValues[i]);
		}
		trace("sender kem decrypt done");

		Encaps(setValues, sender_pk);
		trace("sender encaps done");

		std::vector<std::vector<block>> setValues2; //the setValue for the next OKVS
		setValues2.reserve(sender_pk.size());

		for (size_t i = 0; i < recv_pk.size(); i++) //Simulatable and extraable OKVS
		{
			Bits Y = P.encrypt(KemKeyToBits(sender_pk[i])); //permuation
			std::array<block, KEM_key_block_size> temp = BitsToKemKey(Y);
			setValues2.emplace_back(temp.begin(), temp.end());
			//TODO: need to xor with H(key) -- currently, the size is different
		}

		std::vector<std::vector<block>> okvsTable2(okvsTableSize); 

		SimulatedOkvsEncode(inputSet, setValues2, okvsTable2);
		trace("sender okvs2 encode done");

		//copy and send the okvs to the sender
		for (size_t j = 0; j < okvsTableSize; ++j) {
			// std::memcpy(recv_flat.data() + j * rowOkvsBlkSize, okvsTable[j].data(), rowOkvsBlkSize * sizeof(block));
			std::memcpy(recv_flat.data() + j * rowOkvsBlkSize, okvsTable2[j].data(), rowOkvsBlkSize * sizeof(block));
		}
		trace("sender send okvs2 start");
		chls[0][0]->send(recv_flat.data(), recv_flat.size() * sizeof(block));
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
			pqpsi(pIdx, setSize, mSet);
			});
	}
	for (u64 pIdx = 0; pIdx < pThrds.size(); ++pIdx)
		pThrds[pIdx].join();


}
