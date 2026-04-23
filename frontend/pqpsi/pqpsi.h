#pragma once

#include "Crypto/PRNG.h"
#include "Network/Channel.h"
#include "Network/BtEndpoint.h"
#include <Common/ByteStream.h>
#include <fstream>
#include <util.h>

using namespace osuCrypto;

struct PqPsiStageMs
{
	double totalMs = 0.0;
	double kemKeyGenMs = 0.0;
	double permuteMs = 0.0;
	double okvsEncodeMs = 0.0;
	double okvsDecodeMs = 0.0;
	double networkSendMs = 0.0;
	double networkRecvMs = 0.0;
	double networkSendBytes = 0.0;
	double networkRecvBytes = 0.0;
	double kemCoreMs = 0.0; // encaps or decaps
	double permDecryptMs = 0.0;
};

struct PqPsiRunProfile
{
	PqPsiStageMs party0;
	PqPsiStageMs party1;
};


//void pqpsi(u64 myIdx, u64 setSize, std::vector<block> inputSet);

void PqPsi_Test_Main();
void PqPsi_RbOkvs_Test_Main();
bool PqPsi_RbOkvs_Test_Check(u64& gotIntersection, u64& expectedIntersection);
bool PqPsi_RbOkvs_RunCheck(
	u64 setSize,
	u64& gotIntersection,
	u64& expectedIntersection,
	PqPsiRunProfile* profileOut = nullptr);
