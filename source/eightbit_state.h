#pragma once
#include <string>

struct EightbitState {
	bool broadcastPackets = true;
	uint16_t port = 4000;
	std::string ip = "127.0.0.1";
};
