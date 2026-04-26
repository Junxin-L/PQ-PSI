#pragma once



#include "Network/Channel.h"
#include <fstream>

#define simOkvs 0
#define polyOkvs 1
#define paxosOkvs 2
#define rbOkvs 3

#define SimulatedOkvs simOkvs
#define PolyOkvs polyOkvs
#define PaxosOkvs paxosOkvs
#define RandomBandOkvs rbOkvs
#define	TableOPPRF 0

#define secMalicious 0
#define secSemiHonest 1

// #define okvsHashFunctions 2
// #define okvsLengthScale 2.5
#define okvsHashFunctions 3
#define okvsLengthScale 1.3


#define isNTLThreadSafe 0

void InitDebugPrinting(std::string file = "../testoutput.txt");

void senderGetLatency(osuCrypto::Channel& chl);

void recverGetLatency(osuCrypto::Channel& chl);
