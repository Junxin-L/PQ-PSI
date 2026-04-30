#pragma once

#include "../pqpsi.h"
#include "tools.h"
#include "Network/Channel.h"
#include <functional>
#include <vector>

namespace pqpsi_proto
{
	using namespace osuCrypto;

	struct Ctx
	{
		u64 tableSize = 0;
		u64 bandWidth = 0;
		u64 rowSize = 0;
		bool multiThread = true;
		size_t workerThreads = 4;
		RbCfg rb{};
		KemCfg kem{};
		bool bobPi = false;
		pqperm::Perm& ownPi;
		pqperm::Perm& peerPi;
		std::vector<block>& set;
		std::vector<std::vector<Channel*>>& chls;
		std::function<void(u64)> simSend;
		std::function<void(const char*)> trace;
		PqPsiStageMs& ms;
	};

	void Recv(Ctx& ctx, u64* hitOut);
	void Send(Ctx& ctx);
}
