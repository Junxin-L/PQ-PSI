#include "party.h"

#include "net.h"
#include "tools.h"

namespace pqpsi_proto
{
	using namespace tools;

	void Send(Ctx& ctx)
	{
		//  1: recv OKVS1. Post the receive before local mask work so the
		// network can progress while the sender prepares its own H(x) cache.
		const auto tPrepRecv0 = std::chrono::steady_clock::now();
		std::vector<block> buf(ctx.tableSize * ctx.rowSize);
		const auto tPrepRecv1 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrepRecv1 - tPrepRecv0).count();
		const auto tRecv0 = std::chrono::steady_clock::now();
		const u64 recvBytes = static_cast<u64>(buf.size() * sizeof(block));
		auto okvs1Recv = net::asyncRecv(ctx, 0, buf.data(), recvBytes);

		// local H x cache
		std::vector<block> masks;
		const auto tMask0 = std::chrono::steady_clock::now();
		precomputeMasks(ctx.set, ctx.rowSize, masks, ctx.multiThread, ctx.workerThreads);
		const auto tMask1 = std::chrono::steady_clock::now();
		ctx.ms.maskMs += std::chrono::duration<double, std::milli>(tMask1 - tMask0).count();
		ctx.trace("sender mask cache done");

		okvs1Recv.wait();
		const auto tRecv1 = std::chrono::steady_clock::now();
		ctx.ms.networkRecvMs += std::chrono::duration<double, std::milli>(tRecv1 - tRecv0).count();
		ctx.ms.networkRecvBytes += static_cast<double>(recvBytes);

		//  2: decode OKVS1
		const auto tPrep2 = std::chrono::steady_clock::now();
		std::vector<block> rows(ctx.set.size() * ctx.rowSize);
		const auto tPrep3 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep3 - tPrep2).count();
		const auto tDec0 = std::chrono::steady_clock::now();
		RBDecode(buf, ctx.rowSize, ctx.set, rows, ctx.rb);
		const auto tDec1 = std::chrono::steady_clock::now();
		ctx.ms.okvsDecodeMs += std::chrono::duration<double, std::milli>(tDec1 - tDec0).count();
		ctx.trace("sender okvs1 decode done");

		//  3: xor H x
		//  4: Pi inverse
		const auto tPerm0 = std::chrono::steady_clock::now();
		parallelFor(ctx.set.size(), 16, ctx.multiThread, ctx.workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				block* row = rowPtr(rows, ctx.rowSize, i);
				xorMask(rowPtr(masks, ctx.rowSize, i), row, ctx.rowSize);
				unpermute(ctx.peerPi, row, ctx.rowSize);
			}
		});
		const auto tPerm1 = std::chrono::steady_clock::now();
		ctx.ms.permDecryptMs += std::chrono::duration<double, std::milli>(tPerm1 - tPerm0).count();
		ctx.trace("sender kem decrypt done");

		//  5: encaps
		const auto tPrep4 = std::chrono::steady_clock::now();
		std::vector<block> ct;
		const auto tPrep5 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep5 - tPrep4).count();
		const auto tKem0 = std::chrono::steady_clock::now();
		encapRows(ctx.kem, rows, ctx.rowSize, ct, ctx.multiThread, ctx.workerThreads);
		const auto tKem1 = std::chrono::steady_clock::now();
		ctx.ms.kemCoreMs += std::chrono::duration<double, std::milli>(tKem1 - tKem0).count();
		ctx.trace("sender encaps done");

		//  6: Pi
		//  7: xor H x
		// reuse the decoded row buffer
		const auto tPerm2_0 = std::chrono::steady_clock::now();
		parallelFor(ctx.set.size(), 16, ctx.multiThread, ctx.workerThreads, [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				block* row = rowPtr(rows, ctx.rowSize, i);
				if (ctx.bobPi)
				{
					permute(ctx.ownPi, rowPtr(ct, ctx.rowSize, i), ctx.rowSize, row);
				}
				else
				{
					std::memcpy(row, rowPtr(ct, ctx.rowSize, i), rowBytes(ctx.rowSize));
				}
				xorMask(rowPtr(masks, ctx.rowSize, i), row, ctx.rowSize);
			}
		});
		const auto tPerm2_1 = std::chrono::steady_clock::now();
		ctx.ms.permuteMs += std::chrono::duration<double, std::milli>(tPerm2_1 - tPerm2_0).count();

		//  8: encode OKVS2
		const auto tEnc0 = std::chrono::steady_clock::now();
		RBEncode(ctx.set, rows, ctx.rowSize, buf, ctx.rb);
		const auto tEnc1 = std::chrono::steady_clock::now();
		ctx.ms.okvsEncodeMs += std::chrono::duration<double, std::milli>(tEnc1 - tEnc0).count();
		ctx.trace("sender okvs2 encode done");

		//  9: send OKVS2
		const auto tSend0 = std::chrono::steady_clock::now();
		const u64 sendBytes = static_cast<u64>(buf.size() * sizeof(block));
		ctx.simSend(sendBytes);
		net::send(ctx, 0, buf.data(), sendBytes);
		const auto tSend1 = std::chrono::steady_clock::now();
		ctx.ms.networkSendMs += std::chrono::duration<double, std::milli>(tSend1 - tSend0).count();
		ctx.ms.networkSendBytes += static_cast<double>(sendBytes);
		ctx.trace("sender send okvs2 done");
	}
}
