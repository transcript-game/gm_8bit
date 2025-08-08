#define NO_MALLOC_OVERRIDE

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <scanning/symbolfinder.hpp>
#include <detouring/hook.hpp>
#include <iostream>
#include <iclient.h>
#include <unordered_map>
#include <chrono>
#include "ivoicecodec.h"
#include "audio_effects.h"
#include "net.h"
#include "thirdparty.h"
#include "steam_voice.h"
#include "transcript_state.h"
#include "recorder.h"
#include <GarrysMod/Symbol.hpp>
#include <cstdint>
#include "opus_framedecoder.h"

#define STEAM_PCKT_SZ sizeof(uint64_t) + sizeof(CRC32_t)
#ifdef SYSTEM_WINDOWS
	#include <windows.h>

	const std::vector<Symbol> BroadcastVoiceSyms = {
#if defined ARCHITECTURE_X86
		Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50\x83\x78\x30\x00\x0F\x84****\x53\x8D\x4D\xD8\xC6\x45\xB4\x01\xC7\x45*****"),
		Symbol::FromSignature("\x55\x8B\xEC\x8B\x0D****\x83\xEC\x58\x81\xF9****"),
		Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50"),
#elif defined ARCHITECTURE_X86_64
		Symbol::FromSignature("\x48\x89\x5C\x24*\x56\x57\x41\x56\x48\x81\xEC****\x8B\xF2\x4C\x8B\xF1"),
#endif
	};
#endif

#ifdef SYSTEM_LINUX
	#include <dlfcn.h>
	const std::vector<Symbol> BroadcastVoiceSyms = {
		Symbol::FromName("_Z21SV_BroadcastVoiceDataP7IClientiPcx"),
		Symbol::FromSignature("\x55\x48\x8D\x05****\x48\x89\xE5\x41\x57\x41\x56\x41\x89\xF6\x41\x55\x49\x89\xFD\x41\x54\x49\x89\xD4\x53\x48\x89\xCB\x48\x81\xEC****\x48\x8B\x3D****\x48\x39\xC7\x74\x25"),
	};
#endif

static char decompressedBuffer[20 * 1024];
static char recompressBuffer[20 * 1024];

Net* net_handl = nullptr;
transcriptState* g_transcript = nullptr;

typedef void (*SV_BroadcastVoiceData)(IClient* cl, int nBytes, char* data, int64 xuid);
Detouring::Hook detour_BroadcastVoiceData;

void hook_BroadcastVoiceData(IClient* cl, uint nBytes, char* data, int64 xuid) {
	// Basic runtime signature / argument sanity check: if nBytes is unrealistically small or data null, log once.
	static bool warned_invalid_call = false;
	if ((data == nullptr || nBytes <= 0) && !warned_invalid_call) {
		std::cout << "[transcript][warn] SV_BroadcastVoiceData unusual call: data=" << (void*)data << " nBytes=" << nBytes << std::endl;
		warned_invalid_call = true; // avoid spamming
	}

	//Check if the player is in the set of enabled players.
	//This is (and needs to be) and O(1) operation for how often this function is called.
	//If not in the set, just hit the trampoline to ensure default behavior.
	int uid = cl->GetUserID();

#ifdef THIRDPARTY_LINK
	if(checkIfMuted(cl->GetPlayerSlot()+1)) {
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
	}
#endif

	auto& afflicted_players = g_transcript->afflictedPlayers;
	if (g_transcript->broadcastPackets && nBytes > sizeof(uint64_t)) {
		//Get the user's steamid64, put it at the beginning of the buffer.
		//Notice that we don't use the conveniently provided one in the voice packet. The client can manipulate that one.

#if defined ARCHITECTURE_X86
		uint64_t id64 = *(uint64_t*)((char*)cl + 181);
#else
		uint64_t id64 = *(uint64_t*)((char*)cl + 189);
#endif

		*(uint64_t*)decompressedBuffer = id64;

		//Transfer the packet data to our scratch buffer
		//This looks jank, but it's to prevent a theoretically malformed packet triggering a massive memcpy
		size_t toCopy = nBytes - sizeof(uint64_t);
		std::memcpy(decompressedBuffer + sizeof(uint64_t), data + sizeof(uint64_t), toCopy);

		//Finally we'll broadcast our new packet
 		net_handl->SendPacket(g_transcript->ip.c_str(), g_transcript->port, decompressedBuffer, nBytes);
	}

	// Speaking state tracking using inactivity timeout.
	// Rationale: once a player stops talking, no further packets arrive, so silence streaks never accumulate.
	// We'll mark START on first "audio" packet (heuristic: size > header) and mark STOP when no packets seen for timeout.
	using Clock = std::chrono::steady_clock;
	static const std::chrono::milliseconds stopTimeout(1200);
	bool packetHasAudio = nBytes > (int)STEAM_PCKT_SZ; // crude heuristic
	Clock::time_point now = Clock::now();

	{
		std::lock_guard<std::mutex> lk(g_transcript->speakMtx);
		auto &info = g_transcript->speakInfo[uid];
		info.lastPacket = Clock::now();
		if (packetHasAudio && !info.started) {
			info.started = true;
			g_transcript->currentlySpeaking.insert(uid);
			std::cout << "[transcript] Player " << uid << " START speaking (" << nBytes << " bytes)" << std::endl;
			g_transcript->recorder.Start(uid, 24000);
		}
	}


	if (afflicted_players.find(uid) != afflicted_players.end()) {
		IVoiceCodec* codec = std::get<0>(afflicted_players.at(uid));

		if(nBytes < STEAM_PCKT_SZ) {
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

		int bytesDecompressed = SteamVoice::DecompressIntoBuffer(codec, data, nBytes, decompressedBuffer, sizeof(decompressedBuffer));
		int samples = bytesDecompressed / 2;
		// Submit raw PCM for background encoding (mono 16-bit). Decompressed buffer starts with PCM samples.
		if (samples > 0) {
			g_transcript->recorder.SubmitPCM(uid, reinterpret_cast<int16_t*>(decompressedBuffer), samples, 24000);
		}
		if (bytesDecompressed <= 0) {
			//Just hit the trampoline at this point.
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

		#ifdef _DEBUG
			std::cout << "Decompressed samples " << samples << std::endl;
		#endif

		//Apply audio effect
		int eff = std::get<1>(afflicted_players.at(uid));
		switch (eff) {
		case AudioEffects::EFF_BITCRUSH:
			AudioEffects::BitCrush((uint16_t*)&decompressedBuffer, samples, g_transcript->crushFactor, g_transcript->gainFactor);
			break;
		case AudioEffects::EFF_DESAMPLE:
			AudioEffects::Desample((uint16_t*)&decompressedBuffer, samples, g_transcript->desampleRate);
			break;
		default:
			break;
		}

		//Recompress the stream
		uint64_t steamid = *(uint64_t*)data;
		int bytesWritten = SteamVoice::CompressIntoBuffer(steamid, codec, decompressedBuffer, samples*2, recompressBuffer, sizeof(recompressBuffer), 24000);
		if (bytesWritten <= 0) {
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

		// Submit each contained Opus frame chunk as one packet (our custom container: length+data). Here we only have one contiguous opus payload inside recompressBuffer after headers.
		// recompressBuffer layout: steamid(8) + OP_SAMPLERATE op + rate(2) + OP_CODEC opcode + len(2) + opusdata + crc(4)
		if (bytesWritten > (int)(sizeof(uint64_t)+1+2+1+2+4)) {
			char* ptr = recompressBuffer + sizeof(uint64_t); // after steamid
			// skip samplerate op (1 +2)
			ptr += 1 + 2; // OP_SAMPLERATE
			// opcode OP_CODEC
			ptr += 1; // opcode
			uint16_t opusLen = *(uint16_t*)ptr; ptr += 2;
			if (opusLen > 0 && ptr + opusLen + 4 <= recompressBuffer + bytesWritten) { // +4 for crc at end
				g_transcript->recorder.SubmitOpusPacket(uid, (unsigned char*)ptr, opusLen);
			}
		}

		#ifdef _DEBUG
			std::cout << "Retransmitted pckt size: " << bytesWritten << std::endl;
		#endif

		//Broadcast voice data with our updated compressed data.
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, bytesWritten, recompressBuffer, xuid);
	}
	else {
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
	}
}

LUA_FUNCTION_STATIC(transcript_crush) {
	g_transcript->crushFactor = (int)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(transcript_gain) {
	g_transcript->gainFactor = (float)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(transcript_setbroadcastip) {
	g_transcript->ip = std::string(LUA->GetString());
	return 0;
}

LUA_FUNCTION_STATIC(transcript_setbroadcastport) {
	g_transcript->port = (uint16_t)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(transcript_broadcast) {
	g_transcript->broadcastPackets = LUA->GetBool(1);
	return 0;
}

LUA_FUNCTION_STATIC(transcript_getcrush) {
	LUA->PushNumber(g_transcript->crushFactor);
	return 1;
}

LUA_FUNCTION_STATIC(transcript_setdesamplerate) {
	g_transcript->desampleRate = (int)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(transcript_enableEffect) {
	int id = LUA->GetNumber(1);
	int eff = LUA->GetNumber(2);

	auto& afflicted_players = g_transcript->afflictedPlayers;
	if (afflicted_players.find(id) != afflicted_players.end()) {
		if (eff == AudioEffects::EFF_NONE) {
			IVoiceCodec* codec = std::get<0>(afflicted_players.at(id));
			delete codec;
			afflicted_players.erase(id);
		}
		else {
			std::get<1>(afflicted_players.at(id)) = eff;
		}
		return 0;
	}
	else if(eff != AudioEffects::EFF_NONE) {

		IVoiceCodec* codec = new SteamOpus::Opus_FrameDecoder();
		codec->Init(5, 24000);
		afflicted_players.insert(std::pair<int, std::tuple<IVoiceCodec*, int>>(id, std::tuple<IVoiceCodec*, int>(codec, eff)));
	}
	return 0;
}


GMOD_MODULE_OPEN()
{
	g_transcript = new transcriptState();
	// Launch monitor thread for speaking timeout detection
	g_transcript->monitorThread = std::thread([](){
		using Clock = std::chrono::steady_clock;
		const auto sweepInterval = std::chrono::milliseconds(300);
		const auto stopTimeout = std::chrono::milliseconds(1200);
		while (g_transcript->monitorRunning) {
			std::this_thread::sleep_for(sweepInterval);
			Clock::time_point now = Clock::now();
			std::vector<int> toStop;
			{
				std::lock_guard<std::mutex> lk(g_transcript->speakMtx);
				for (auto &p : g_transcript->speakInfo) {
					if (p.second.started && (now - p.second.lastPacket) > stopTimeout) {
						toStop.push_back(p.first);
					}
				}
				for (int sid : toStop) {
					auto it = g_transcript->speakInfo.find(sid);
					if (it != g_transcript->speakInfo.end()) {
						it->second.started = false;
					}
					if (g_transcript->currentlySpeaking.find(sid) != g_transcript->currentlySpeaking.end()) {
						std::cout << "[transcript] Player " << sid << " STOP speaking (timeout)" << std::endl;
						g_transcript->currentlySpeaking.erase(sid);
						g_transcript->recorder.Stop(sid);
					}
				}
			}
		}
	});

	SourceSDK::ModuleLoader engine_loader("engine");
	SymbolFinder symfinder;

	void* sv_bcast = nullptr;

	for (const auto& sym : BroadcastVoiceSyms) {
		sv_bcast = symfinder.Resolve(engine_loader.GetModule(), sym.name.c_str(), sym.length);

		if (sv_bcast)
			break;
	}

	if (sv_bcast == nullptr) {
		LUA->ThrowError("Could not locate SV_BroadcastVoice symbol!");
	}

	detour_BroadcastVoiceData.Create(Detouring::Hook::Target(sv_bcast), reinterpret_cast<void*>(&hook_BroadcastVoiceData));
	detour_BroadcastVoiceData.Enable();

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);

	LUA->PushString("transcript");
	LUA->CreateTable();
		LUA->PushString("SetCrushFactor");
		LUA->PushCFunction(transcript_crush);
		LUA->SetTable(-3);

		LUA->PushString("GetCrushFactor");
		LUA->PushCFunction(transcript_getcrush);
		LUA->SetTable(-3);

		LUA->PushString("EnableEffect");
		LUA->PushCFunction(transcript_enableEffect);
		LUA->SetTable(-3);

		LUA->PushString("EnableBroadcast");
		LUA->PushCFunction(transcript_broadcast);
		LUA->SetTable(-3);

		LUA->PushString("SetGainFactor");
		LUA->PushCFunction(transcript_gain);
		LUA->SetTable(-3);

		LUA->PushString("SetDesampleRate");
		LUA->PushCFunction(transcript_setdesamplerate);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastIP");
		LUA->PushCFunction(transcript_setbroadcastip);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastPort");
		LUA->PushCFunction(transcript_setbroadcastport);
		LUA->SetTable(-3);

		LUA->PushString("EFF_NONE");
		LUA->PushNumber(AudioEffects::EFF_NONE);
		LUA->SetTable(-3);

		LUA->PushString("EFF_DESAMPLE");
		LUA->PushNumber(AudioEffects::EFF_DESAMPLE);
		LUA->SetTable(-3);

		LUA->PushString("EFF_BITCRUSH");
		LUA->PushNumber(AudioEffects::EFF_BITCRUSH);
		LUA->SetTable(-3);
	LUA->SetTable(-3);
	LUA->Pop();

	net_handl = new Net();

#ifdef THIRDPARTY_LINK
	linkMutedFunc();
#endif

	return 0;
}

GMOD_MODULE_CLOSE()
{
	g_transcript->monitorRunning = false;
	if (g_transcript->monitorThread.joinable()) g_transcript->monitorThread.join();
	detour_BroadcastVoiceData.Disable();
	detour_BroadcastVoiceData.Destroy();

	for (auto& p : g_transcript->afflictedPlayers) {
		IVoiceCodec* codec = std::get<0>(p.second);
		if (codec != nullptr) {
			delete codec;
		}
	}

	delete net_handl;
	delete g_transcript;

	return 0;
}
