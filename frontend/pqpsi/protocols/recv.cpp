#include "party.h"

#include "tools.h"

namespace pqpsi_proto
{
	using namespace tools;

	void Recv(Ctx& ctx, u64* hitOut)
	{
		// step 1 keygen
		std::vector<kemKey> sk(ctx.set.size());
		std::vector<kemKey> pk(ctx.set.size());
		const auto tKey0 = std::chrono::steady_clock::now();
		genKeys(sk, pk, ctx.multiThread);
		const auto tKey1 = std::chrono::steady_clock::now();
		ctx.ms.kemKeyGenMs += std::chrono::duration<double, std::milli>(tKey1 - tKey0).count();
		ctx.trace("genKeys done");

		// local H x cache
		std::vector<kemKey> masks;
		precomputeMasks(ctx.set, masks, ctx.multiThread);
		ctx.trace("receiver mask cache done");

		// step 2 permute pk
		// step 3 xor H x
		std::vector<std::vector<block>> rows(pk.size());
		const auto tPerm0 = std::chrono::steady_clock::now();
		parallelFor(rows.size(), 16, ctx.multiThread, [&](size_t begin, size_t end)
		{
			Bits bits;
			bits.reserve(KEM_key_size_bit);
			for (size_t i = begin; i < end; ++i)
			{
				permute(ctx.pi, pk[i], rows[i], bits);
				xorMask(masks[i], rows[i]);
			}
		});
		const auto tPerm1 = std::chrono::steady_clock::now();
		ctx.ms.permuteMs += std::chrono::duration<double, std::milli>(tPerm1 - tPerm0).count();

		// step 4 encode OKVS1
		std::vector<block> buf(ctx.tableSize * ctx.rowSize);
		const auto tEnc0 = std::chrono::steady_clock::now();
		RBEncode(ctx.set, rows, buf, ctx.rowSize, ctx.rb);
		const auto tEnc1 = std::chrono::steady_clock::now();
		ctx.ms.okvsEncodeMs += std::chrono::duration<double, std::milli>(tEnc1 - tEnc0).count();
		ctx.trace("receiver okvs1 encode done");

		// step 5 send OKVS1
		const auto tSend0 = std::chrono::steady_clock::now();
		const u64 sendBytes = static_cast<u64>(buf.size() * sizeof(block));
		ctx.simSend(sendBytes);
		ctx.chls[1][0]->send(buf.data(), sendBytes);
		const auto tSend1 = std::chrono::steady_clock::now();
		ctx.ms.networkSendMs += std::chrono::duration<double, std::milli>(tSend1 - tSend0).count();
		ctx.ms.networkSendBytes += static_cast<double>(sendBytes);

		// step 6 recv OKVS2
		const auto tRecv0 = std::chrono::steady_clock::now();
		const u64 recvBytes = static_cast<u64>(buf.size() * sizeof(block));
		ctx.chls[1][0]->recv(buf.data(), recvBytes);
		const auto tRecv1 = std::chrono::steady_clock::now();
		ctx.ms.networkRecvMs += std::chrono::duration<double, std::milli>(tRecv1 - tRecv0).count();
		ctx.ms.networkRecvBytes += static_cast<double>(recvBytes);

		// step 7 decode OKVS2
		// reuse the first row buffer: after OKVS1 is encoded it is no longer needed.
		const auto tDec0 = std::chrono::steady_clock::now();
		RBDecode(buf, ctx.rowSize, ctx.set, rows, ctx.rb);
		const auto tDec1 = std::chrono::steady_clock::now();
		ctx.ms.okvsDecodeMs += std::chrono::duration<double, std::milli>(tDec1 - tDec0).count();
		ctx.trace("receiver okvs2 decode done");

		// step 8 xor H x
		// step 9 Pi inverse
		const auto tPerm2_0 = std::chrono::steady_clock::now();
		parallelFor(rows.size(), 16, ctx.multiThread, [&](size_t begin, size_t end)
		{
			Bits bits;
			bits.reserve(KEM_key_size_bit);
			for (size_t i = begin; i < end; ++i)
			{
				xorMask(masks[i], rows[i]);
				unpermute(ctx.pi, rows[i], bits);
			}
		});
		const auto tPerm2_1 = std::chrono::steady_clock::now();
		ctx.ms.permDecryptMs += std::chrono::duration<double, std::milli>(tPerm2_1 - tPerm2_0).count();

		// step 10 decaps
		const auto tKem0 = std::chrono::steady_clock::now();
		const u64 hits = countDecapHits(sk, rows, ctx.multiThread);
		const auto tKem1 = std::chrono::steady_clock::now();
		ctx.ms.kemCoreMs += std::chrono::duration<double, std::milli>(tKem1 - tKem0).count();
		ctx.trace("receiver decaps done");
		if (hitOut)
		{
			*hitOut = hits;
		}
	}
}
