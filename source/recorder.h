// Asynchronous per-player voice recorder encoding PCM to Opus packets (length-prefixed) in a background thread.
#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cstdint>

struct OpusEncoder; // forward (we will create dynamically via opus headers already present)

struct RecordingTask {
    int uid;
    std::vector<int16_t> pcm; // mono 16-bit samples
    int sampleRate;
};

struct RecordingSession {
    OpusEncoder* encoder = nullptr;
    FILE* file = nullptr;
};

class RecorderManager {
public:
    RecorderManager();
    ~RecorderManager();

    void Start(int uid, int sampleRate = 24000);
    void SubmitPCM(int uid, const int16_t* samples, size_t count, int sampleRate = 24000);
    // Submit an already Opus encoded frame (length + data format). Avoids decoding/re-encoding path.
    void SubmitOpusPacket(int uid, const unsigned char* data, size_t len);
    void Stop(int uid);

private:
    void Worker();
    void EncodeAndWrite(RecordingSession& session, const int16_t* samples, size_t count, int sampleRate);
    std::string MakeFilename(int uid) const;

    std::unordered_map<int, RecordingSession> sessions;
    std::mutex mtx;
    std::queue<RecordingTask> tasks;
    std::condition_variable cv;
    std::thread worker;
    std::atomic<bool> running{true};
};
