#pragma once

#include "pqpsi.h"

#include <algorithm>

struct PqPsiPhaseMs
{
	double alice1 = 0.0;
	double netDA = 0.0;
	double bob2 = 0.0;
	double netDB = 0.0;
	double alice3 = 0.0;

	double total() const
	{
		return alice1 + netDA + bob2 + netDB + alice3;
	}
};

inline double pqpsiProtocolMs(const PqPsiStageMs& ms)
{
	const double v = ms.totalMs - ms.setupMs - ms.teardownMs;
	return v > 0.0 ? v : 0.0;
}

inline double pqpsiLocalMs(const PqPsiStageMs& ms)
{
	return ms.prepMs
		+ ms.kemKeyGenMs
		+ ms.maskMs
		+ ms.permuteMs
		+ ms.permDecryptMs
		+ ms.kemCoreMs
		+ ms.okvsEncodeMs
		+ ms.okvsDecodeMs;
}

inline double pqpsiMsgMs(double bytes, double oneWayDelayMs, double bwMBps)
{
	double ms = bytes > 0.0 ? std::max(0.0, oneWayDelayMs) : 0.0;
	if (bytes > 0.0 && bwMBps > 0.0)
	{
		ms += (bytes / (bwMBps * 1000.0 * 1000.0)) * 1000.0;
	}
	return ms;
}

inline double pqpsiAlice1Ms(const PqPsiStageMs& alice)
{
	return alice.prepMs
		+ alice.kemKeyGenMs
		+ alice.maskMs
		+ alice.permuteMs
		+ alice.okvsEncodeMs;
}

inline double pqpsiBob2Ms(const PqPsiStageMs& bob)
{
	return bob.prepMs
		+ bob.maskMs
		+ bob.okvsDecodeMs
		+ bob.permDecryptMs
		+ bob.kemCoreMs
		+ bob.permuteMs
		+ bob.okvsEncodeMs;
}

inline double pqpsiAlice3Ms(const PqPsiStageMs& alice)
{
	return alice.okvsDecodeMs
		+ alice.permDecryptMs
		+ alice.kemCoreMs;
}

inline PqPsiPhaseMs pqpsiModel(
	const PqPsiRunProfile& prof,
	double oneWayDelayMs,
	double bwMBps)
{
	PqPsiPhaseMs ms;
	ms.alice1 = pqpsiAlice1Ms(prof.party0);
	ms.netDA = pqpsiMsgMs(prof.party0.networkSendBytes, oneWayDelayMs, bwMBps);
	ms.bob2 = pqpsiBob2Ms(prof.party1);
	ms.netDB = pqpsiMsgMs(prof.party1.networkSendBytes, oneWayDelayMs, bwMBps);
	ms.alice3 = pqpsiAlice3Ms(prof.party0);
	return ms;
}
