#include "params.h"

#define PKlength KYBER_INDCPA_PUBLICKEYBYTES
#define PKlengthsmall KYBER_POLYVECBYTES
#define SKlength KYBER_INDCPA_SECRETKEYBYTES
#define CTlength KYBER_INDCPA_BYTES
#define OTlength KYBER_INDCPA_MSGBYTES
#define Coinslength KYBER_SYMBYTES

typedef struct
{
    unsigned char sm[2][CTlength];
} KyberOTCtxt;

typedef struct
{
    unsigned char sot[2][OTlength];
} KyberOTPtxt;

typedef struct
{
    unsigned char keys[2][PKlength];
} KyberOtRecvPKs;

typedef struct
{
    unsigned char secretKey[SKlength];
    unsigned char rot[OTlength];
    unsigned char b;
} KyberOTRecver;

void KyberReceiverMessage(KyberOTRecver* recver, KyberOtRecvPKs* pks);
void KyberSenderMessage(KyberOTCtxt* ctxt, KyberOTPtxt* ptxt, KyberOtRecvPKs* recvPks);
void KyberReceiverStrings(KyberOTRecver* recver, KyberOTCtxt* ctxt);

int KyberExample();
