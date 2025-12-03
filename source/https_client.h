#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>

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
    std::atomic<uint32_t> m_sequence_counter{0};
    uint32_t m_pending_first_sequence = 0;
    uint32_t m_pending_last_sequence = 0;
    size_t m_pending_packet_count = 0;
    std::chrono::steady_clock::time_point m_last_voice_packet;
    bool m_next_starts_stream = true;

    std::mutex m_buffer_mutex;
    std::mutex m_send_mutex;
    std::thread m_flush_thread;
    std::atomic<bool> m_running{false};
    // Keep batches large enough to carry ~1s of audio for up to ~128 players while staying under server limit (10MB).
    static constexpr size_t MAX_BUFFER_SIZE = 8 * 1024 * 1024; // 8MB
    static constexpr int FLUSH_INTERVAL_MS = 1000; // target ~1 request/sec per game server
    static constexpr int START_GAP_MS = 1200; // treat gaps over this as new mic press

    bool SendBufferedPackets();
    bool SendBufferedPacketsLocked(std::unique_lock<std::mutex>& lock);
    bool PerformSend(const std::vector<char>& payload, size_t packet_count, uint32_t first_seq, uint32_t last_seq);
    void StartFlushThread();
    void StopFlushThread();

    // Platform-specific handle
#ifdef _WIN32
    void* m_session;
    void* m_connect;
#else
    void* m_curl;
#endif
};
