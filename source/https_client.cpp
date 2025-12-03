#include "https_client.h"
#include "debug.h"
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

HttpsClient::HttpsClient(const std::string& base_url, const std::string& bearer_token)
    : m_base_url(base_url), m_bearer_token(bearer_token) {

    // Parse host/path from base_url, defaulting to /api/voice/stream
    std::string url = base_url;
    const std::string default_path = "/api/voice/stream";
    // strip scheme
    size_t scheme_pos = url.find("://");
    if (scheme_pos != std::string::npos) {
        url = url.substr(scheme_pos + 3);
    }
    size_t slash_pos = url.find('/');
    if (slash_pos != std::string::npos) {
        m_host = url.substr(0, slash_pos);
    } else {
        m_host = url;
    }
    m_path = default_path;

#ifdef _WIN32
    m_session = WinHttpOpen(
        L"gm_8bit/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!m_session) {
        DEBUG_LOG("Failed to open WinHTTP session");
        m_connect = nullptr;
        return;
    }

    std::wstring host_w(m_host.begin(), m_host.end());
    // Connect to remote host
    m_connect = WinHttpConnect((HINTERNET)m_session, host_w.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!m_connect) {
        DEBUG_LOG("Failed to connect to " << m_host);
    } else {
        m_stream_request = nullptr;
        InitStream();
    }
#else
    curl_global_init(CURL_GLOBAL_DEFAULT);
    m_curl = nullptr;
    InitStream();
#endif
}

bool HttpsClient::SendInit() {
#ifdef _WIN32
    if (!m_connect) return false;

    HINTERNET request = WinHttpOpenRequest(
        (HINTERNET)m_connect,
        L"POST",
        L"/api/init",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (!request) {
        DEBUG_LOG("Failed to create HTTP request");
        return false;
    }

    // Add Authorization header
    std::wstring bearer_token_wide(m_bearer_token.begin(), m_bearer_token.end());
    std::wstring auth_header = L"Authorization: Bearer " + bearer_token_wide;
    WinHttpAddRequestHeaders(
        request,
        auth_header.c_str(),
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );

    // Add additional headers
    WinHttpAddRequestHeaders(
        request,
        L"Content-Type: application/json",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );

    WinHttpAddRequestHeaders(
        request,
        L"X-Client-Id: gm_8bit",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );

    // Empty JSON body
    const char* body = "{}";
    DWORD bodyLen = 2;

    // Send request
    BOOL result = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)body,
        bodyLen,
        bodyLen,
        0
    );

    if (result) {
        result = WinHttpReceiveResponse(request, NULL);
    }

    if (!result) {
        DEBUG_LOG("Init request failed: " << GetLastError());
    } else {
        DEBUG_LOG("Init request sent successfully");
    }

    WinHttpCloseHandle(request);
    return result != 0;

#else
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = m_base_url + "/api/init";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // Set Bearer token and headers
    struct curl_slist* headers = NULL;
    std::string auth = "Authorization: Bearer " + m_bearer_token;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "X-Client-Id: gm_8bit");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Empty JSON body
    const char* body = "{}";
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 2L);

    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        DEBUG_LOG("Init request failed: " << curl_easy_strerror(res));
        return false;
    }

    DEBUG_LOG("Init request sent successfully");
    return true;
#endif
}

bool HttpsClient::SendVoicePacket(const char* data, uint32_t len) {
    return SendFrame(data, len);
}

void HttpsClient::FlushBuffer() {
    // No-op in streaming mode
}

HttpsClient::~HttpsClient() {
#ifdef _WIN32
    if (m_stream_request) WinHttpCloseHandle((HINTERNET)m_stream_request);
    if (m_connect) WinHttpCloseHandle((HINTERNET)m_connect);
    if (m_session) WinHttpCloseHandle((HINTERNET)m_session);
#else
    if (m_curl) {
        curl_easy_cleanup(m_curl);
        curl_global_cleanup();
    }
#endif
}

bool HttpsClient::InitStream() {
#ifdef _WIN32
    if (!m_connect) return false;

    if (m_stream_request) {
        WinHttpCloseHandle((HINTERNET)m_stream_request);
        m_stream_request = nullptr;
    }

    std::wstring path_w(m_path.begin(), m_path.end());
    HINTERNET request = WinHttpOpenRequest(
        (HINTERNET)m_connect,
        L"POST",
        path_w.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (!request) {
        DEBUG_LOG("Failed to create streaming HTTP request");
        return false;
    }

    // Add Authorization header
    std::wstring bearer_token_wide(m_bearer_token.begin(), m_bearer_token.end());
    std::wstring auth_header = L"Authorization: Bearer " + bearer_token_wide;
    WinHttpAddRequestHeaders(
        request,
        auth_header.c_str(),
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );

    // Add headers for streaming
    WinHttpAddRequestHeaders(
        request,
        L"Content-Type: application/octet-stream",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );
    WinHttpAddRequestHeaders(
        request,
        L"Transfer-Encoding: chunked",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );
    WinHttpAddRequestHeaders(
        request,
        L"Connection: keep-alive",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );
    WinHttpAddRequestHeaders(
        request,
        L"X-Client-Id: gm_8bit",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );

    BOOL result = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );

    if (!result) {
        DEBUG_LOG("Failed to start streaming request: " << GetLastError());
        WinHttpCloseHandle(request);
        return false;
    }

    m_stream_request = request;
    m_stream_seq = 0;
    DEBUG_LOG("Streaming channel initialized");
    return true;
#else
    // Recreate handle
    if (m_curl) {
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
    m_stream_connected = false;
    m_curl = curl_easy_init();
    if (!m_curl) {
        DEBUG_LOG("Failed to initialize libcurl for streaming");
        return false;
    }

    std::string url = m_base_url + m_path;
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(m_curl, CURLOPT_CONNECT_ONLY, 1L);

    CURLcode res = curl_easy_perform(m_curl);
    if (res != CURLE_OK) {
        DEBUG_LOG("Failed to establish streaming connection: " << curl_easy_strerror(res));
        return false;
    }

    // Manually send the HTTP request for chunked upload
    std::string request =
        "POST " + m_path + " HTTP/1.1\r\n" +
        "Host: " + m_host + "\r\n" +
        "Authorization: Bearer " + m_bearer_token + "\r\n" +
        "Content-Type: application/octet-stream\r\n" +
        "Transfer-Encoding: chunked\r\n" +
        "Connection: keep-alive\r\n" +
        "X-Client-Id: gm_8bit\r\n\r\n";

    size_t sent = 0;
    while (sent < request.size()) {
        size_t nsent = 0;
        res = curl_easy_send(m_curl, request.data() + sent, request.size() - sent, &nsent);
        if (res != CURLE_OK) {
            DEBUG_LOG("Failed to send streaming HTTP headers: " << curl_easy_strerror(res));
            return false;
        }
        sent += nsent;
    }

    m_stream_connected = true;
    m_stream_seq = 0;
    DEBUG_LOG("Streaming channel initialized (connect_only)");
    return true;
#endif
}

bool HttpsClient::SendFrame(const char* data, uint32_t len) {
#ifdef _WIN32
    if (!m_stream_request && !InitStream()) {
        return false;
    }
#else
    if (!m_stream_connected && !InitStream()) {
        return false;
    }
#endif

    // Build frame: [uint32 seq][uint64 sentAtNs][uint64 steamId][uint32 packetLen][payload]
    const uint32_t seq = m_stream_seq++;
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
    uint64_t steam_id = 0;
    if (len >= sizeof(uint64_t)) {
        std::memcpy(&steam_id, data, sizeof(uint64_t));
    }

    const size_t header_size = sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint32_t);
    std::vector<char> frame(header_size + len);

    std::memcpy(frame.data(), &seq, sizeof(uint32_t));
    std::memcpy(frame.data() + sizeof(uint32_t), &now_ns, sizeof(uint64_t));
    std::memcpy(frame.data() + sizeof(uint32_t) + sizeof(uint64_t), &steam_id, sizeof(uint64_t));
    std::memcpy(frame.data() + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t), &len, sizeof(uint32_t));
    std::memcpy(frame.data() + header_size, data, len);

#ifdef _WIN32
    auto write_once = [&](HINTERNET req) -> bool {
        DWORD written = 0;
        BOOL ok = WinHttpWriteData(
            req,
            frame.data(),
            (DWORD)frame.size(),
            &written
        );
        return ok && written == frame.size();
    };

    if (write_once((HINTERNET)m_stream_request)) {
        return true;
    }

    DEBUG_LOG("Failed to write streaming frame, attempting to re-init stream");
    if (InitStream() && write_once((HINTERNET)m_stream_request)) {
        return true;
    }

    DEBUG_LOG("Streaming frame failed after retry");
    return false;
#else
    if (!m_curl || !m_stream_connected) {
        return false;
    }

    // Manually format chunk: <hex size>\r\n<data>\r\n
    std::ostringstream oss;
    oss << std::hex << frame.size();
    std::string size_hex = oss.str();
    std::string chunk = size_hex + "\r\n";
    chunk.append(frame.begin(), frame.end());
    chunk.append("\r\n");

    const char* chunk_data = chunk.data();
    size_t chunk_len = chunk.size();
    size_t sent_total = 0;

    auto send_chunk = [&](void) -> bool {
        size_t sent_total_inner = 0;
        while (sent_total_inner < chunk_len) {
            size_t nsent = 0;
            CURLcode res = curl_easy_send(m_curl, chunk_data + sent_total_inner, chunk_len - sent_total_inner, &nsent);
            if (res != CURLE_OK) {
                DEBUG_LOG("Failed to send streaming chunk: " << curl_easy_strerror(res));
                return false;
            }
            sent_total_inner += nsent;
        }
        return true;
    };

    if (send_chunk()) {
        return true;
    }

    // Retry once after re-init
    m_stream_connected = false;
    if (InitStream() && send_chunk()) {
        return true;
    }

    m_stream_connected = false;
    DEBUG_LOG("Streaming chunk failed after retry");
    return false;
#endif
}
