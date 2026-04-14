#include "pqpsi.h"
#include "Crypto/PRNG.h"
#include "Common/Defines.h"
#include "Common/Log.h"
#include "Common/Log1.h"
#include <set>
#include "okvs.h"
#include <fstream>
#include <util.h>
#include <permutation.h>


using namespace osuCrypto;



void Encaps(std::vector<std::vector<block>>& input, std::vector<kemKey>& out)
{
	out.resize(input.size());

	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i].size() != KEM_key_block_size) {
			throw std::invalid_argument("Encaps: wrong inner vector size");
		}

		std::copy(input[i].begin(), input[i].end(), out[i].begin());
	}
	//TODO: write Encaps here

}

bool Decaps(kemKey sk, std::vector<block> value)
{
	//TODO write the decaps
	return 1;
}

void pqpsi(u64 myIdx, u64 setSize, std::vector<block> inputSet)
{
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
	
	for (size_t i = 0; i < inputSet.size(); i++) //TODO: get the real keys from KEM
	{
		for (size_t j = 0; j < recv_sk[0].size(); j++)
		{
			recv_sk[i][j] = ZeroBlock;
			recv_pk[i][j] = ZeroBlock;
			sender_pk[i][j] = ZeroBlock;
		}
	
	}

	
	//##########################
	//### OKVS Encoding
	//##########################

	u64 okvsTableSize= okvsLengthScale * inputSet.size(); //depending on okvs variant
	u64 rowOkvsBlkSize = KEM_key_block_size;
	int s = Keccak_size_bit; //permuation parameter -- need to update if needed
	ConstructionPermutation P(Keccak_size_bit, KEM_key_size_bit, s, Keccak1600Adapter::pi, Keccak1600Adapter::pi_inv);


	if (myIdx == 0) //receiver
	{
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

		 // send okvs
		std::vector<block> flat(okvsTableSize * rowOkvsBlkSize);

		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy(flat.data() + j * rowOkvsBlkSize, okvsTable[j].data(), rowOkvsBlkSize * sizeof(block));
		}


		chls[1][0]->send(flat.data(), flat.size() * sizeof(block));
		

		/////receving the second OKVS

		std::vector<std::vector<block>> okvsTable2(okvsTableSize,std::vector<block>(rowOkvsBlkSize));
		std::vector<std::vector<block>> setValues2(inputSet.size());

		chls[1][0]->recv(flat.data(), flat.size() * sizeof(block));
		
		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy( okvsTable[j].data(), flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
		}
		

		SimulatedOkvsDecode(okvsTable2, inputSet, setValues2);
		for (u32 i = 0; i < inputSet.size(); i++)
		{
			if (Decaps(recv_sk[i], setValues2[i]))
				mIntersection.push_back(i);
		}


	}
	else if (myIdx == 1) //sender
	{
		std::vector<block> recv_flat(okvsTableSize * rowOkvsBlkSize);
		chls[0][0]->recv(recv_flat.data(), recv_flat.size() * sizeof(block));

		std::vector<std::vector<block>> okvsTable(okvsTableSize,
		std::vector<block>(rowOkvsBlkSize));

		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy(okvsTable[j].data(), recv_flat.data() + j * rowOkvsBlkSize, rowOkvsBlkSize * sizeof(block));
		}

		std::vector<std::vector<block>> setValues(inputSet.size());

		
	    SimulatedOkvsDecode(okvsTable,inputSet, setValues);

		Encaps(setValues, sender_pk);

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

		//copy and send the okvs to the sender
		for (size_t j = 0; j < okvsTableSize; ++j) {
			std::memcpy(recv_flat.data() + j * rowOkvsBlkSize, okvsTable[j].data(), rowOkvsBlkSize * sizeof(block));
		}
		chls[0][0]->send(recv_flat.data(), recv_flat.size() * sizeof(block));

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
