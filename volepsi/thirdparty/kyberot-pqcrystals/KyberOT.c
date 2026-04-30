#include "KyberOT.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fips202.h"
#include "indcpa.h"
#include "poly.h"
#include "polyvec.h"
#include "randombytes.h"

static void pack_pk(uint8_t r[KYBER_INDCPA_PUBLICKEYBYTES],
                    const polyvec* pk,
                    const uint8_t seed[KYBER_SYMBYTES])
{
    polyvec_tobytes(r, pk);
    memcpy(r + KYBER_POLYVECBYTES, seed, KYBER_SYMBYTES);
}

static void unpack_pk(polyvec* pk,
                      uint8_t seed[KYBER_SYMBYTES],
                      const uint8_t packedpk[KYBER_INDCPA_PUBLICKEYBYTES])
{
    polyvec_frombytes(pk, packedpk);
    memcpy(seed, packedpk + KYBER_POLYVECBYTES, KYBER_SYMBYTES);
}

static void pkPlus(uint8_t pk[PKlength], const uint8_t pk1[PKlength], const uint8_t pk2[PKlength])
{
    polyvec pkpv1;
    polyvec pkpv2;
    uint8_t seed[KYBER_SYMBYTES];

    unpack_pk(&pkpv1, seed, pk1);
    unpack_pk(&pkpv2, seed, pk2);
    polyvec_add(&pkpv1, &pkpv1, &pkpv2);
    polyvec_reduce(&pkpv1);
    pack_pk(pk, &pkpv1, seed);
}

static void polyvec_sub_local(polyvec* r, const polyvec* a, const polyvec* b)
{
    unsigned int i;
    for (i = 0; i < KYBER_K; ++i)
        poly_sub(&r->vec[i], &a->vec[i], &b->vec[i]);
}

static void pkMinus(uint8_t pk[PKlength], const uint8_t pk1[PKlength], const uint8_t pk2[PKlength])
{
    polyvec pkpv1;
    polyvec pkpv2;
    uint8_t seed[KYBER_SYMBYTES];

    unpack_pk(&pkpv1, seed, pk1);
    unpack_pk(&pkpv2, seed, pk2);
    polyvec_sub_local(&pkpv1, &pkpv1, &pkpv2);
    polyvec_reduce(&pkpv1);
    pack_pk(pk, &pkpv1, seed);
}

static void randomPK(uint8_t pk[PKlength], const uint8_t seed1[KYBER_SYMBYTES], const uint8_t seed2[KYBER_SYMBYTES])
{
    polyvec a[KYBER_K];
    gen_matrix(a, seed1, 0);
    pack_pk(pk, &a[0], seed2);
}

static void pkHash(uint8_t h[PKlength], const uint8_t pk[PKlength], const uint8_t publicseed[KYBER_SYMBYTES])
{
    uint8_t seed[KYBER_SYMBYTES];
    sha3_256(seed, pk, PKlengthsmall);
    randomPK(h, seed, publicseed);
}

void KyberReceiverMessage(KyberOTRecver* recver, KyberOtRecvPKs* pks)
{
    uint8_t pk[PKlength];
    uint8_t h[PKlength];
    uint8_t seed[KYBER_SYMBYTES];
    uint8_t coins[KYBER_SYMBYTES];

    randombytes(coins, sizeof(coins));
    indcpa_keypair_derand(pk, recver->secretKey, coins);

    randombytes(seed, sizeof(seed));
    randomPK(pks->keys[1 ^ recver->b], seed, pk + PKlengthsmall);

    pkHash(h, pks->keys[1 ^ recver->b], pk + PKlengthsmall);
    pkMinus(pks->keys[recver->b], pk, h);
}

void KyberSenderMessage(KyberOTCtxt* ctxt, KyberOTPtxt* ptxt, KyberOtRecvPKs* recvPks)
{
    uint8_t h[PKlength];
    uint8_t pk[PKlength];
    uint8_t coins[Coinslength];

    randombytes(coins, sizeof(coins));
    pkHash(h, recvPks->keys[1], recvPks->keys[0] + PKlengthsmall);
    pkPlus(pk, recvPks->keys[0], h);
    indcpa_enc(ctxt->sm[0], ptxt->sot[0], pk, coins);

    randombytes(coins, sizeof(coins));
    pkHash(h, recvPks->keys[0], recvPks->keys[0] + PKlengthsmall);
    pkPlus(pk, recvPks->keys[1], h);
    indcpa_enc(ctxt->sm[1], ptxt->sot[1], pk, coins);
}

void KyberReceiverStrings(KyberOTRecver* recver, KyberOTCtxt* ctxt)
{
    indcpa_dec(recver->rot, ctxt->sm[recver->b], recver->secretKey);
}

int KyberExample()
{
    KyberOTCtxt ctxt;
    KyberOTPtxt ptxt;
    KyberOTRecver recver;
    KyberOtRecvPKs pks;

    memset(&ctxt, 0, sizeof(ctxt));
    memset(&ptxt, 0, sizeof(ptxt));
    memset(&recver, 0, sizeof(recver));
    memset(&pks, 0, sizeof(pks));

    randombytes(&recver.b, 1);
    recver.b &= 1;
    randombytes(ptxt.sot[0], OTlength);
    randombytes(ptxt.sot[1], OTlength);

    KyberReceiverMessage(&recver, &pks);
    KyberSenderMessage(&ctxt, &ptxt, &pks);
    KyberReceiverStrings(&recver, &ctxt);

    return memcmp(recver.rot, ptxt.sot[recver.b], OTlength) == 0 ? 0 : 1;
}
