#pragma once

#include "xboxInternals.h"

class network
{
public:
	static void init();
	static void restart();
	static bool isReady();
	static uint32_t getAddress();
	static uint32_t getSubnet();
	static uint32_t getGateway();
	static uint32_t getPrimaryDns();
	static uint32_t getSecondaryDns();
	static bool getNetworkInitialized();
	static void setNetworkInitialized(bool value);
private:
	static void configure();
};
