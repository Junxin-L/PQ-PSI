#pragma once



#include "Network/Channel.h"
#include <fstream>

#define rbOkvs 3

#define RandomBandOkvs rbOkvs

#define okvsHashFunctions 3
#define okvsLengthScale 1.3

void InitDebugPrinting(std::string file = "../testoutput.txt");

void senderGetLatency(osuCrypto::Channel& chl);

void recverGetLatency(osuCrypto::Channel& chl);
