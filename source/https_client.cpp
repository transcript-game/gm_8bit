#include "https_client.h"
#include "debug.h"
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

namespace {
inline void append_uint32_le(std::vector<char>& buffer, uint32_t value) {
	for (int i = 0; i < 4; ++i) {
		buffer.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
	}
}

inline void append_uint64_le(std::vector<char>& buffer, uint64_t value) {
	for (int i = 0; i < 8; ++i) {
		buffer.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
	}
}
} // namespace

HttpsClient::HttpsClient(const std::string& base_url, const std::string& bearer_token)
    : m_base_url(base_url), m_bearer_token(bearer_token) {

    // Initialize buffer and timer
    m_packet_buffer.reserve(MAX_BUFFER_SIZE);
    m_last_flush = std::chrono::steady_clock::now();

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

    // Connect to transcript.linv.dev
    m_connect = WinHttpConnect((HINTERNET)m_session, L"transcript.linv.dev", INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!m_connect) {
        DEBUG_LOG("Failed to connect to transcript.linv.dev");
    }
#else
    curl_global_init(CURL_GLOBAL_DEFAULT);
    m_curl = curl_easy_init();

    if (!m_curl) {
        DEBUG_LOG("Failed to initialize libcurl");
    }
#endif

    StartFlushThread();
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
    if (!m_curl) return false;

    std::string url = m_base_url + "/api/init";
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_POST, 1L);

    // Set Bearer token and headers
    struct curl_slist* headers = NULL;
    std::string auth = "Authorization: Bearer " + m_bearer_token;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "X-Client-Id: gm_8bit");
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headers);

    // Empty JSON body
    const char* body = "{}";
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, 2L);

    // Set timeout
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(m_curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        DEBUG_LOG("Init request failed: " << curl_easy_strerror(res));
        return false;
    }

    DEBUG_LOG("Init request sent successfully");
    return true;
#endif
}

bool HttpsClient::SendVoicePacket(const char* data, uint32_t len) {
    uint64_t steam_id = 0;
    std::memcpy(&steam_id, data, sizeof(uint64_t));

    const uint32_t sequence = ++m_sequence_counter;
    const uint64_t capture_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    const auto now = std::chrono::steady_clock::now();
    bool should_flush = false;
    size_t buffered_size = 0;
    size_t buffered_packets = 0;

    {
        std::lock_guard<std::mutex> lock(m_buffer_mutex);

        append_uint64_le(m_packet_buffer, steam_id);
        append_uint32_le(m_packet_buffer, sequence);
        append_uint64_le(m_packet_buffer, capture_ms);
        append_uint32_le(m_packet_buffer, len);
        m_packet_buffer.insert(m_packet_buffer.end(), data, data + len);

        if (m_pending_packet_count == 0) {
            m_pending_first_sequence = sequence;
        }
        m_pending_last_sequence = sequence;
        m_pending_packet_count += 1;

        buffered_size = m_packet_buffer.size();
        buffered_packets = m_pending_packet_count;

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_flush).count();
        should_flush = (buffered_size >= MAX_BUFFER_SIZE) || (elapsed >= FLUSH_INTERVAL_MS);
    }

    DEBUG_LOG("Buffered voice packet seq " << sequence << " for steamid " << steam_id
        << " (" << len << " bytes, buffer=" << buffered_size << " bytes, packets=" << buffered_packets << ")");

    if (should_flush) {
        return SendBufferedPackets();
    }

    return true; // Buffered successfully
}

void HttpsClient::FlushBuffer() {
    SendBufferedPackets();
}

bool HttpsClient::SendBufferedPackets() {
    std::unique_lock<std::mutex> lock(m_buffer_mutex);
    return SendBufferedPacketsLocked(lock);
}

bool HttpsClient::SendBufferedPacketsLocked(std::unique_lock<std::mutex>& lock) {
    if (m_packet_buffer.empty()) {
        return true;
    }

    std::vector<char> payload;
    payload.swap(m_packet_buffer);

    const size_t packet_count = m_pending_packet_count;
    const uint32_t first_seq = m_pending_first_sequence;
    const uint32_t last_seq = m_pending_last_sequence;

    m_pending_packet_count = 0;
    m_pending_first_sequence = 0;
    m_pending_last_sequence = 0;

    lock.unlock();
    const bool result = PerformSend(payload, packet_count, first_seq, last_seq);
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> relock(m_buffer_mutex);
    m_last_flush = now;

    return result;
}

bool HttpsClient::PerformSend(const std::vector<char>& payload, size_t packet_count, uint32_t first_seq, uint32_t last_seq) {
    if (payload.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> send_lock(m_send_mutex);

    DEBUG_LOG("Sending HTTPS POST batch (" << payload.size() << " bytes) seq " << first_seq << "-" << last_seq
        << " packets=" << packet_count);

#ifdef _WIN32
    if (!m_connect) {
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        (HINTERNET)m_connect,
        L"POST",
        L"/api/voice",
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

    // Add Content-Type header
    WinHttpAddRequestHeaders(
        request,
        L"Content-Type: application/octet-stream",
        (DWORD)-1L,
        WINHTTP_ADDREQ_FLAG_ADD
    );

    // Send request
    BOOL result = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)payload.data(),
        payload.size(),
        payload.size(),
        0
    );

    if (result) {
        result = WinHttpReceiveResponse(request, NULL);
    }

    if (!result) {
        DEBUG_LOG("HTTPS POST failed: " << GetLastError());
    }

    WinHttpCloseHandle(request);
    return result != 0;

#else
    if (!m_curl) {
        return false;
    }

    std::string url = m_base_url + "/api/voice";
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, payload.data());
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));

    // Set Bearer token
    struct curl_slist* headers = NULL;
    std::string auth = "Authorization: Bearer " + m_bearer_token;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headers);

    // Set SSL options
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Set timeout
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(m_curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        DEBUG_LOG("HTTPS POST failed: " << curl_easy_strerror(res));
        return false;
    }

    return true;
#endif
}

void HttpsClient::StartFlushThread() {
    m_running = true;
    m_flush_thread = std::thread([this]() {
        while (m_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!m_running) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            size_t buffered = 0;
            long elapsed_ms = 0;

            {
                std::lock_guard<std::mutex> lock(m_buffer_mutex);
                buffered = m_packet_buffer.size();
                elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_flush).count();
            }

            if (buffered > 0 && elapsed_ms >= FLUSH_INTERVAL_MS) {
                SendBufferedPackets();
            }
        }
    });
}

void HttpsClient::StopFlushThread() {
    m_running = false;
    if (m_flush_thread.joinable()) {
        m_flush_thread.join();
    }
}

HttpsClient::~HttpsClient() {
    StopFlushThread();

    // Flush any remaining buffered packets
    FlushBuffer();

#ifdef _WIN32
    if (m_connect) WinHttpCloseHandle((HINTERNET)m_connect);
    if (m_session) WinHttpCloseHandle((HINTERNET)m_session);
#else
    if (m_curl) {
        curl_easy_cleanup(m_curl);
        curl_global_cleanup();
    }
#endif
}
