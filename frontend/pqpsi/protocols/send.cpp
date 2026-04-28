#include "party.h"

#include "tools.h"

namespace pqpsi_proto
{
	using namespace tools;

	void Send(Ctx& ctx)
	{
		// local H x cache
		std::vector<kemKey> masks;
		const auto tMask0 = std::chrono::steady_clock::now();
		precomputeMasks(ctx.set, masks, ctx.multiThread, ctx.workerThreads);
		const auto tMask1 = std::chrono::steady_clock::now();
		ctx.ms.maskMs += std::chrono::duration<double, std::milli>(tMask1 - tMask0).count();
		ctx.trace("sender mask cache done");

		// step 1 recv OKVS1
		const auto tPrep0 = std::chrono::steady_clock::now();
		std::vector<block> buf(ctx.tableSize * ctx.rowSize);
		const auto tPrep1 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep1 - tPrep0).count();
		const auto tRecv0 = std::chrono::steady_clock::now();
		const u64 recvBytes = static_cast<u64>(buf.size() * sizeof(block));
		ctx.chls[0][0]->recv(buf.data(), recvBytes);
		const auto tRecv1 = std::chrono::steady_clock::now();
		ctx.ms.networkRecvMs += std::chrono::duration<double, std::milli>(tRecv1 - tRecv0).count();
		ctx.ms.networkRecvBytes += static_cast<double>(recvBytes);

		// step 2 decode OKVS1
		const auto tPrep2 = std::chrono::steady_clock::now();
		std::vector<block> rows(ctx.set.size() * ctx.rowSize);
		const auto tPrep3 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep3 - tPrep2).count();
		const auto tDec0 = std::chrono::steady_clock::now();
		RBDecode(buf, ctx.rowSize, ctx.set, rows, ctx.rb);
		const auto tDec1 = std::chrono::steady_clock::now();
		ctx.ms.okvsDecodeMs += std::chrono::duration<double, std::milli>(tDec1 - tDec0).count();
		ctx.trace("sender okvs1 decode done");

		// step 3 xor H x
		// step 4 Pi inverse
		const auto tPerm0 = std::chrono::steady_clock::now();
		parallelFor(ctx.set.size(), 16, ctx.multiThread, ctx.workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				block* row = rowPtr(rows, ctx.rowSize, i);
				xorMask(masks[i], row);
				unpermute(ctx.peerPi, row);
			}
		});
		const auto tPerm1 = std::chrono::steady_clock::now();
		ctx.ms.permDecryptMs += std::chrono::duration<double, std::milli>(tPerm1 - tPerm0).count();
		ctx.trace("sender kem decrypt done");

		// step 5 encaps
		const auto tPrep4 = std::chrono::steady_clock::now();
		std::vector<kemKey> ct(ctx.set.size());
		const auto tPrep5 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep5 - tPrep4).count();
		const auto tKem0 = std::chrono::steady_clock::now();
		encap(rows, ctx.rowSize, ct, ctx.multiThread, ctx.workerThreads);
		const auto tKem1 = std::chrono::steady_clock::now();
		ctx.ms.kemCoreMs += std::chrono::duration<double, std::milli>(tKem1 - tKem0).count();
		ctx.trace("sender encaps done");

		// step 6 Pi
		// step 7 xor H x
		// reuse the decoded row buffer: after encaps it is no longer needed.
		const auto tPerm2_0 = std::chrono::steady_clock::now();
		parallelFor(ctx.set.size(), 16, ctx.multiThread, ctx.workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				block* row = rowPtr(rows, ctx.rowSize, i);
				permute(ctx.ownPi, ct[i], row);
				xorMask(masks[i], row);
			}
		});
		const auto tPerm2_1 = std::chrono::steady_clock::now();
		ctx.ms.permuteMs += std::chrono::duration<double, std::milli>(tPerm2_1 - tPerm2_0).count();

		// step 8 encode OKVS2
		const auto tEnc0 = std::chrono::steady_clock::now();
		RBEncode(ctx.set, rows, ctx.rowSize, buf, ctx.rb);
		const auto tEnc1 = std::chrono::steady_clock::now();
		ctx.ms.okvsEncodeMs += std::chrono::duration<double, std::milli>(tEnc1 - tEnc0).count();
		ctx.trace("sender okvs2 encode done");

		// step 9 send OKVS2
		const auto tSend0 = std::chrono::steady_clock::now();
		const u64 sendBytes = static_cast<u64>(buf.size() * sizeof(block));
		ctx.simSend(sendBytes);
		ctx.chls[0][0]->send(buf.data(), sendBytes);
		const auto tSend1 = std::chrono::steady_clock::now();
		ctx.ms.networkSendMs += std::chrono::duration<double, std::milli>(tSend1 - tSend0).count();
		ctx.ms.networkSendBytes += static_cast<double>(sendBytes);
		ctx.trace("sender send okvs2 done");
	}
}
