#pragma once
#include <cstdint>
#include <string>

class HttpsClient {
public:
    HttpsClient(const std::string& base_url, const std::string& bearer_token);
    ~HttpsClient();
    
    bool SendVoicePacket(const char* data, uint32_t len);
    
private:
    std::string m_base_url;
    std::string m_bearer_token;
    
    // Platform-specific handle
#ifdef _WIN32
    void* m_session;
    void* m_connect;
#else
    void* m_curl;
#endif
};
