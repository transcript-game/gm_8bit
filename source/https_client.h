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
    void FlushBuffer(); // Deprecated (no-op in streaming mode)

private:
    std::string m_base_url;
    std::string m_bearer_token;
    uint32_t m_stream_seq = 0;

    // Parsed URL components for streaming
    std::string m_host;
    std::string m_path; // defaults to /api/voice/stream

    bool InitStream();
    bool SendFrame(const char* data, uint32_t len);

    // Platform-specific handle
#ifdef _WIN32
    void* m_session;
    void* m_connect;
    void* m_stream_request;
#else
    void* m_curl;
    bool m_stream_connected = false;
#endif
};
