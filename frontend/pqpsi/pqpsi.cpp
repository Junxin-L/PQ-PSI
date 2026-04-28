#include "pqpsi.h"
#include "protocols/party.h"
#include "Crypto/PRNG.h"
#include "Common/Defines.h"
#include "Common/Log.h"
#include "Common/Log1.h"
#include "util.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace osuCrypto;
using namespace pqpsi_proto;

void pqpsi(
	u64 me,
	u64 setSize,
	std::vector<block> set,
	const RbCfg* rb,
	u64* hitOut,
	PqPsiStageMs* msOut,
	const PiCfg* piCfg)
{
	(void)setSize;

	using Clock = std::chrono::steady_clock;
	auto ms = [](const Clock::time_point& a, const Clock::time_point& b)
	{
		return std::chrono::duration<double, std::milli>(b - a).count();
	};

	PqPsiStageMs local{};
	const auto all0 = Clock::now();
	const auto setup0 = Clock::now();

	const bool traceOn = []()
	{
		const char* v = std::getenv("PQPSI_TRACE");
		return v && *v && *v != '0';
	}();
	auto trace = [&](const char* msg)
	{
		if (!traceOn) return;
		std::cerr << "[pqpsi][p" << me << "] " << msg << std::endl;
	};

	auto envDouble = [](const char* key, double fallback)
	{
		const char* v = std::getenv(key);
		if (!v || !*v) return fallback;
		try
		{
			return std::stod(v);
		}
		catch (...)
		{
			return fallback;
		}
	};

	struct NetCfg
	{
		double delayMs = 0.0;
		double bwMBps = 0.0;
	};

	const NetCfg net{
		envDouble("PQPSI_SIM_NET_DELAY_MS", 0.0),
		envDouble("PQPSI_SIM_NET_BW_MBPS", 0.0)
	};

	auto simSend = [&](u64 bytes)
	{
		double waitMs = std::max(0.0, net.delayMs);
		if (net.bwMBps > 0.0)
		{
			const double bytesPerSec = net.bwMBps * 1024.0 * 1024.0;
			waitMs += (static_cast<double>(bytes) / bytesPerSec) * 1000.0;
		}
		if (waitMs > 0.0)
		{
			std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(waitMs));
		}
	};

	const u64 nParties = 2;
	const u64 nCh = 1;

	std::string runName("psi");
	if (const char* tag = std::getenv("PQPSI_RUN_TAG"))
	{
		if (*tag)
		{
			runName += "_";
			runName += tag;
		}
	}

	u32 portBase = 1210;
	if (const char* v = std::getenv("PQPSI_PORT_BASE"))
	{
		try
		{
			portBase = static_cast<u32>(std::stoul(v));
		}
		catch (...) {}
	}

	BtIOService ios(0);
	std::vector<BtEndpoint> eps(nParties);
	std::vector<std::vector<Channel*>> chs(nParties);

	for (u64 i = 0; i < nParties; ++i)
	{
		if (i == me) continue;

		const bool isServer = (i > me);
		const u32 port = isServer
			? static_cast<u32>(portBase + me * 10 + i)
			: static_cast<u32>(portBase + i * 10 + me);
		eps[i].start(ios, "localhost", port, isServer, runName);

		chs[i].resize(nCh);
		for (u64 j = 0; j < nCh; ++j)
		{
			const std::string chName = runName + "_ch" + std::to_string(j);
			chs[i][j] = &eps[i].addChannel(chName, chName);
		}
	}

	RbCfg rbCfg{};
	RbInfo rbInfo{};
	if (rb)
	{
		rbCfg = *rb;
	}
	rbInfo = RbOkvsResolve(set.size(), rbCfg);
	const u64 tableSize = static_cast<u64>(rbInfo.columns);
	const u64 bandWidth = static_cast<u64>(rbInfo.bandWidth);
	bool multiThread = true;
	multiThread = rbCfg.multiThread;
	const size_t workerThreads = rbInfo.workerThreads;

	const u64 rowSize = KEM_key_block_size;
	PiCfg localPi{};
	if (piCfg)
	{
		localPi = *piCfg;
	}
	const u8 ownParty = static_cast<u8>(me & 1U);
	const u8 peerParty = static_cast<u8>(ownParty ^ 1U);
	auto ownPi = makePi(localPi, ownParty);
	auto peerPi = makePi(localPi, peerParty);

	Ctx ctx{
		tableSize,
		bandWidth,
		rowSize,
		multiThread,
		workerThreads,
		rbCfg,
		*ownPi,
		*peerPi,
		set,
		chs,
		simSend,
		trace,
		local
	};
	local.setupMs = ms(setup0, Clock::now());

	if (me == 0)
	{
		Recv(ctx, hitOut);
	}
	else if (me == 1)
	{
		Send(ctx);
	}

	const auto teardown0 = Clock::now();
	for (u64 i = 0; i < nParties; ++i)
	{
		if (i == me) continue;
		for (u64 j = 0; j < nCh; ++j)
		{
			chs[i][j]->close();
		}
		eps[i].stop();
	}
	ios.stop();
	local.teardownMs = ms(teardown0, Clock::now());

	local.totalMs = ms(all0, Clock::now());
	if (msOut)
	{
		*msOut = local;
	}
}
