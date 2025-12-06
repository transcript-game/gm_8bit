#pragma once
#include <string>

struct VoiceTranscriptState {
	// Default values - will be overridden by Lua config if available
	std::string api_url = "https://api.voice-transcript.com";
	std::string bearer_token = "example_static_bearer_token_12345";
};
