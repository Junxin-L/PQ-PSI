#include "party.h"

#include "net.h"
#include "tools.h"

namespace pqpsi_proto
{
	using namespace tools;

	void Recv(Ctx &ctx, u64 *hitOut)
	{
		//  1: keygen
		const auto tPrep0 = std::chrono::steady_clock::now();
		std::vector<RawKey> sk;
		std::vector<block> pk(ctx.set.size() * ctx.rowSize);
		const auto tPrep1 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep1 - tPrep0).count();
		const auto tKey0 = std::chrono::steady_clock::now();
		genRows(ctx.kem, sk, pk, ctx.rowSize, ctx.multiThread, ctx.workerThreads);
		const auto tKey1 = std::chrono::steady_clock::now();
		ctx.ms.kemKeyGenMs += std::chrono::duration<double, std::milli>(tKey1 - tKey0).count();
		ctx.trace("genKeys done");

		// local H x cache
		std::vector<block> masks;
		const auto tMask0 = std::chrono::steady_clock::now();
		precomputeMasks(ctx.set, ctx.rowSize, masks, ctx.multiThread, ctx.workerThreads);
		const auto tMask1 = std::chrono::steady_clock::now();
		ctx.ms.maskMs += std::chrono::duration<double, std::milli>(tMask1 - tMask0).count();
		ctx.trace("receiver mask cache done");

		//  2: permute pk
		//  3: xor H x
		const auto tPrep2 = std::chrono::steady_clock::now();
		std::vector<block> rows(ctx.set.size() * ctx.rowSize);
		const auto tPrep3 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep3 - tPrep2).count();
		const auto tPerm0 = std::chrono::steady_clock::now();
		parallelFor(ctx.set.size(), 16, ctx.multiThread, ctx.workerThreads, [&](size_t begin, size_t end)
					{
			for (size_t i = begin; i < end; ++i)
			{
				block* row = rowPtr(rows, ctx.rowSize, i);
				permute(ctx.ownPi, rowPtr(pk, ctx.rowSize, i), ctx.rowSize, row);
				xorMask(rowPtr(masks, ctx.rowSize, i), row, ctx.rowSize);
			} });
		const auto tPerm1 = std::chrono::steady_clock::now();
		ctx.ms.permuteMs += std::chrono::duration<double, std::milli>(tPerm1 - tPerm0).count();

		//  4: encode OKVS1
		const auto tPrep4 = std::chrono::steady_clock::now();
		std::vector<block> buf(ctx.tableSize * ctx.rowSize);
		const auto tPrep5 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrep5 - tPrep4).count();
		const auto tEnc0 = std::chrono::steady_clock::now();
		RBEncode(ctx.set, rows, ctx.rowSize, buf, ctx.rb);
		const auto tEnc1 = std::chrono::steady_clock::now();
		ctx.ms.okvsEncodeMs += std::chrono::duration<double, std::milli>(tEnc1 - tEnc0).count();
		ctx.trace("receiver okvs1 encode done");

		//  6: recv OKVS2. Post this before sending OKVS1 so the receive is
		// already armed when the sender finishes its response.
		const auto tPrepRecv0 = std::chrono::steady_clock::now();
		std::vector<block> recvBuf(buf.size());
		const auto tPrepRecv1 = std::chrono::steady_clock::now();
		ctx.ms.prepMs += std::chrono::duration<double, std::milli>(tPrepRecv1 - tPrepRecv0).count();
		const auto tRecv0 = std::chrono::steady_clock::now();
		const u64 recvBytes = static_cast<u64>(recvBuf.size() * sizeof(block));
		auto okvs2Recv = net::asyncRecv(ctx, 1, recvBuf.data(), recvBytes);

		//  5: send OKVS1
		const auto tSend0 = std::chrono::steady_clock::now();
		const u64 sendBytes = static_cast<u64>(buf.size() * sizeof(block));
		ctx.simSend(sendBytes);
		net::send(ctx, 1, buf.data(), sendBytes);
		const auto tSend1 = std::chrono::steady_clock::now();
		ctx.ms.networkSendMs += std::chrono::duration<double, std::milli>(tSend1 - tSend0).count();
		ctx.ms.networkSendBytes += static_cast<double>(sendBytes);

		okvs2Recv.wait();
		const auto tRecv1 = std::chrono::steady_clock::now();
		ctx.ms.networkRecvMs += std::chrono::duration<double, std::milli>(tRecv1 - tRecv0).count();
		ctx.ms.networkRecvBytes += static_cast<double>(recvBytes);

		//  7: decode OKVS2
		// reuse the first row buffer, after OKVS1 is encoded it is no longer needed.
		const auto tDec0 = std::chrono::steady_clock::now();
		RBDecode(recvBuf, ctx.rowSize, ctx.set, rows, ctx.rb);
		const auto tDec1 = std::chrono::steady_clock::now();
		ctx.ms.okvsDecodeMs += std::chrono::duration<double, std::milli>(tDec1 - tDec0).count();
		ctx.trace("receiver okvs2 decode done");

		//  8: xor H x
		//  9: Pi inverse
		const auto tPerm2_0 = std::chrono::steady_clock::now();
		parallelFor(ctx.set.size(), 16, ctx.multiThread, ctx.workerThreads, [&](size_t begin, size_t end)
					{
			for (size_t i = begin; i < end; ++i)
			{
				block* row = rowPtr(rows, ctx.rowSize, i);
				xorMask(rowPtr(masks, ctx.rowSize, i), row, ctx.rowSize);
				if (ctx.bobPi)
				{
					unpermute(ctx.peerPi, row, ctx.rowSize);
				}
			} });
		const auto tPerm2_1 = std::chrono::steady_clock::now();
		ctx.ms.permDecryptMs += std::chrono::duration<double, std::milli>(tPerm2_1 - tPerm2_0).count();

		//  1:0 decaps
		const auto tKem0 = std::chrono::steady_clock::now();
		const u64 hits = countHits(ctx.kem, sk, rows, ctx.rowSize, ctx.multiThread, ctx.workerThreads);
		const auto tKem1 = std::chrono::steady_clock::now();
		ctx.ms.kemCoreMs += std::chrono::duration<double, std::milli>(tKem1 - tKem0).count();
		ctx.trace("receiver decaps done");
		if (hitOut)
		{
			*hitOut = hits;
		}
	}
}
