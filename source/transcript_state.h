#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "recorder.h"
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>

struct transcriptState {
	int crushFactor = 350;
	float gainFactor = 1.2;
	bool broadcastPackets = false;
	int desampleRate = 2;
	uint16_t port = 4000;
	std::string ip = "127.0.0.1";
	std::unordered_map<int, std::tuple<IVoiceCodec*, int>> afflictedPlayers;
	//Tracks which userids we currently consider to be actively sending voice data
	std::unordered_set<int> currentlySpeaking;
	RecorderManager recorder;
	// Speaking tracking for async timeout detection
	struct SpeakInfo { std::chrono::steady_clock::time_point lastPacket; bool started = false; };
	std::unordered_map<int, SpeakInfo> speakInfo;
	std::mutex speakMtx;
	std::thread monitorThread;
	bool monitorRunning = true;
};
