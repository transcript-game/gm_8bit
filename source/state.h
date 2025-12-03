#pragma once
#include <string>

struct EightbitState {
	bool broadcastPackets = true;
	std::string api_url = "https://transcript.linv.dev";
	std::string bearer_token = "example_static_bearer_token_12345";
};
