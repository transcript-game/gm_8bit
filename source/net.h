#pragma once
#include <cstdint>

class Net {
public:
	Net();
	~Net();
	void SendPacket(const char* dest, uint16_t port, const char* buffer, uint32_t len);

private:
	int m_socket;
	// Static encryption key - can be modified directly in compiled DLL
	static constexpr char ENCRYPTION_KEY[65] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
};