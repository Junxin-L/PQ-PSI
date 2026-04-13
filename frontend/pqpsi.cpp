#include "pqpsi.h"
#include "Crypto/PRNG.h"
#include "Common/Defines.h"
#include "Common/Log.h"
#include "Common/Log1.h"
#include <set>
#include "okvs.h"
#include <fstream>
#include <util.h>


using namespace osuCrypto;




void pqpsi(u64 myIdx, u64 setSize, std::vector<block> set)
{
	u64  psiSecParam = 40, bitSize = 128, numThreads = 1, nParties = 2;
	PRNG prng(_mm_set_epi32(4253465, 3434565, 234435, 23987045));

	PRNG prng1(_mm_set_epi32(4253465, myIdx, myIdx, myIdx)); //for test
	if (myIdx == 0)
		set[2] = prng1.get<block>();;


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

	//##########################
	//### OKVS Encoding
	//##########################

	auto start = timer.setTimePoint("start");

	


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
	u64 setSize = 1 << 8, psiSecParam = 40, bitSize = 128;
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
