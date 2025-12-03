#include "https_client.h"
#include "debug.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

HttpsClient::HttpsClient(const std::string& base_url, const std::string& bearer_token)
    : m_base_url(base_url), m_bearer_token(bearer_token) {

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
}

bool HttpsClient::SendVoicePacket(const char* data, uint32_t len) {
#ifdef _WIN32
    if (!m_connect) return false;

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
        (LPVOID)data,
        len,
        len,
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
    if (!m_curl) return false;

    std::string url = m_base_url + "/api/voice";
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, len);

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

HttpsClient::~HttpsClient() {
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
