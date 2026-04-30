#include "pqpsi.h"
#include "protocols/party.h"
#include "Crypto/PRNG.h"
#include "Common/Defines.h"
#include "Common/Log.h"
#include "Common/Log1.h"
#include "util.h"

#include <algorithm>
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
	const PiCfg* piCfg,
	const KemCfg* kemCfg)
{
	if (setSize != set.size())
	{
		throw std::invalid_argument("pqpsi: setSize must match set.size()");
	}

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

	auto envSize = [](const char* key, size_t fallback)
	{
		const char* v = std::getenv(key);
		if (!v || !*v) return fallback;
		try
		{
			return static_cast<size_t>(std::stoull(v));
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

	const bool disableAppNetSim = []()
	{
		const char* v = std::getenv("PQPSI_DISABLE_APP_NET_SIM");
		return v && *v && *v != '0';
	}();

	auto simSend = [&](u64 bytes)
	{
		if (disableAppNetSim)
		{
			return;
		}

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

	RbCfg rbCfg{};
	if (rb)
	{
		rbCfg = *rb;
	}
	const RbInfo rbInfo = RbOkvsResolve(set.size(), rbCfg);
	const u64 tableSize = static_cast<u64>(rbInfo.columns);
	const u64 bandWidth = static_cast<u64>(rbInfo.bandWidth);
	const bool multiThread = rbCfg.multiThread;
	const size_t workerThreads = rbInfo.workerThreads;
	size_t requestedChannels = envSize("PQPSI_NET_CHANNELS", workerThreads);
	if (requestedChannels == 0)
	{
		requestedChannels = workerThreads;
	}
	const size_t netChannels = multiThread
		? std::max<size_t>(1, std::min(requestedChannels, workerThreads))
		: 1;

	const u64 nParties = 2;
	const u64 nCh = static_cast<u64>(netChannels);

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

	// Force all TCP/channel handshakes before protocol timing. This matches the
	// other PSI benchmarks, where connection/setup traffic is excluded.
	auto warmChannels = [&]()
	{
		u8 sendByte = static_cast<u8>(0xA0 | (me & 1));
		u8 recvByte = 0;
		for (u64 peer = 0; peer < nParties; ++peer)
		{
			if (peer == me) continue;
			for (u64 j = 0; j < chs[peer].size(); ++j)
			{
				if (me < peer)
				{
					chs[peer][j]->send(&sendByte, 1);
					chs[peer][j]->recv(&recvByte, 1);
				}
				else
				{
					chs[peer][j]->recv(&recvByte, 1);
					chs[peer][j]->send(&sendByte, 1);
				}
			}
		}
	};
	warmChannels();

	KemCfg localKem{};
	if (kemCfg)
	{
		localKem = *kemCfg;
	}

	const u64 rowSize = static_cast<u64>(kemRowBlocks(localKem));
	PiCfg localPi{};
	if (piCfg)
	{
		localPi = *piCfg;
	}
	if (localKem.kind == PsiKemKind::EcKem)
	{
		localPi.kind = pqperm::Kind::Xoodoo;
	}
	else if (localPi.kind == pqperm::Kind::Xoodoo)
	{
		throw std::invalid_argument("xoodoo permutation is only supported with eckem");
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
		localKem,
		localPi.bobPi,
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
