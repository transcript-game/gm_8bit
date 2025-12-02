#define NO_MALLOC_OVERRIDE

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <scanning/symbolfinder.hpp>
#include <detouring/hook.hpp>
#include <iclient.h>
#include <cstring>
#include <vector>
#include "debug.h"
#include "net.h"
#include "thirdparty.h"
#include "eightbit_state.h"
#include <GarrysMod/Symbol.hpp>
#include <cstdint>

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

Net* net_handl = nullptr;
EightbitState* g_eightbit = nullptr;

typedef void (*SV_BroadcastVoiceData)(IClient* cl, int nBytes, char* data, int64 xuid);
Detouring::Hook detour_BroadcastVoiceData;

void hook_BroadcastVoiceData(IClient* cl, uint nBytes, char* data, int64 xuid) {
	int uid = cl->GetUserID();
	DEBUG_LOG("Voice packet received for uid " << uid << " (" << nBytes << " bytes)");

#ifdef THIRDPARTY_LINK
	if(checkIfMuted(cl->GetPlayerSlot()+1)) {
		DEBUG_LOG("Skipping muted player uid " << uid << " due to third-party mute");
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
	}
#endif

	if (g_eightbit->broadcastPackets && nBytes > sizeof(uint64_t)) {
		if (nBytes > sizeof(decompressedBuffer)) {
			DEBUG_LOG("Packet too large to mirror (" << nBytes << " bytes); skipping relay");
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

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
		DEBUG_LOG("Broadcasting packet for steamid " << id64 << " to " << g_eightbit->ip << ":" << g_eightbit->port << " (" << nBytes << " bytes)");
 		net_handl->SendPacket(g_eightbit->ip.c_str(), g_eightbit->port, decompressedBuffer, nBytes);
	}

	return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
}

LUA_FUNCTION_STATIC(eightbit_setbroadcastip) {
	g_eightbit->ip = std::string(LUA->GetString());
	DEBUG_LOG("SetBroadcastIP to " << g_eightbit->ip);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setbroadcastport) {
	g_eightbit->port = (uint16_t)LUA->GetNumber(1);
	DEBUG_LOG("SetBroadcastPort to " << g_eightbit->port);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_broadcast) {
	g_eightbit->broadcastPackets = LUA->GetBool(1);
	DEBUG_LOG("EnableBroadcast set to " << g_eightbit->broadcastPackets);
	return 0;
}

GMOD_MODULE_OPEN()
{
	g_eightbit = new EightbitState();
	DEBUG_LOG("gm_8bit initializing; debug logging is enabled");

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
	DEBUG_LOG("Attached SV_BroadcastVoiceData hook");

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);

	LUA->PushString("eightbit");
	LUA->CreateTable();

		LUA->PushString("EnableBroadcast");
		LUA->PushCFunction(eightbit_broadcast);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastIP");
		LUA->PushCFunction(eightbit_setbroadcastip);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastPort");
		LUA->PushCFunction(eightbit_setbroadcastport);
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
	DEBUG_LOG("gm_8bit shutting down");
	detour_BroadcastVoiceData.Disable();
	detour_BroadcastVoiceData.Destroy();
	DEBUG_LOG("Detached SV_BroadcastVoiceData hook");

	delete net_handl;
	delete g_eightbit;
	DEBUG_LOG("Cleanup complete");

	return 0;
}
