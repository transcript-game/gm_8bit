#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

class HttpsClient {
public:
    HttpsClient(const std::string& base_url, const std::string& bearer_token);
    ~HttpsClient();

    bool SendInit();
    bool SendVoicePacket(const char* data, uint32_t len);
    void FlushBuffer(); // Force send buffered packets

private:
    std::string m_base_url;
    std::string m_bearer_token;

    // Buffering for batch requests - optimized for transcription (not real-time)
    std::vector<char> m_packet_buffer;
    std::chrono::steady_clock::time_point m_last_flush;
    static constexpr size_t MAX_BUFFER_SIZE = 1 * 1024 * 1024; // 1MB (reduced for more frequent flushes)
    static constexpr int FLUSH_INTERVAL_MS = 1000; // 1 second (increased from 100ms)

    bool SendBufferedPackets();

    // Platform-specific handle
#ifdef _WIN32
    void* m_session;
    void* m_connect;
#else
    void* m_curl;
#endif
};
