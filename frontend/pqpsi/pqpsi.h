#pragma once

#include "Crypto/PRNG.h"
#include "Network/Channel.h"
#include "Network/BtEndpoint.h"
#include "okvs/rbokvs.h"
#include <limits>
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

using RbCfg = osuCrypto::RBParams;
using RbInfo = osuCrypto::RBResolved;

void pqpsi(
	u64 me,
	u64 setSize,
	std::vector<block> set,
	const RbCfg* rb = nullptr,
	u64* hitOut = nullptr,
	PqPsiStageMs* msOut = nullptr);

void psiMain();
void rbMain();
bool rbCheck(u64& got, u64& want, const RbCfg* rb = nullptr);
bool rbRun(
	u64 setSize,
	u64& got,
	u64& want,
	const RbCfg* rb = nullptr,
	PqPsiRunProfile* out = nullptr,
	u64 hitTarget = std::numeric_limits<u64>::max());
