#pragma once



#include "Network/Channel.h"
#include <fstream>

#define SimulatedOkvs 0
#define PolyOkvs 1
#define PaxosOkvs 2
#define RandomBandOkvs 3
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
