#include "recorder.h"
// Use bundled opus headers (located in opus/include). Premake already adds that include dir, so just include <opus.h>.
#include <opus.h>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <iomanip>

RecorderManager::RecorderManager() {
    worker = std::thread(&RecorderManager::Worker, this);
}

RecorderManager::~RecorderManager() {
    running = false;
    cv.notify_all();
    if (worker.joinable()) worker.join();
    // cleanup sessions
    for (auto &p : sessions) {
        if (p.second.encoder) opus_encoder_destroy(p.second.encoder);
        if (p.second.file) fclose(p.second.file);
    }
}

void RecorderManager::Start(int uid, int sampleRate) {
    std::lock_guard<std::mutex> lock(mtx);
    if (sessions.find(uid) != sessions.end()) return; // already

    int err = 0;
    OpusEncoder* enc = opus_encoder_create(sampleRate, 1, OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK) return; // failed
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(32000));

    std::string fname = MakeFilename(uid);
    FILE* f = fopen(fname.c_str(), "wb");
    if (!f) {
        opus_encoder_destroy(enc);
        return;
    }
    // Simple header: magic + version
    const char magic[8] = {'O','P','U','S','P','K','T','1'};
    fwrite(magic, 1, sizeof(magic), f);
    fflush(f);
    sessions[uid] = RecordingSession{enc, f};
}

void RecorderManager::SubmitPCM(int uid, const int16_t* samples, size_t count, int sampleRate) {
    std::lock_guard<std::mutex> lock(mtx);
    if (sessions.find(uid) == sessions.end()) return; // not recording
    RecordingTask t; t.uid = uid; t.sampleRate = sampleRate; t.pcm.assign(samples, samples + count);
    tasks.push(std::move(t));
    cv.notify_one();
}

void RecorderManager::SubmitOpusPacket(int uid, const unsigned char* data, size_t len) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = sessions.find(uid);
    if (it == sessions.end()) return;
    // Direct write (length prefix + data)
    uint16_t sz = (uint16_t)len;
    fwrite(&sz, sizeof(uint16_t), 1, it->second.file);
    fwrite(data, 1, len, it->second.file);
    fflush(it->second.file);
}

void RecorderManager::Stop(int uid) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = sessions.find(uid);
    if (it == sessions.end()) return;
    if (it->second.encoder) opus_encoder_destroy(it->second.encoder);
    if (it->second.file) fclose(it->second.file);
    sessions.erase(it);
}

void RecorderManager::Worker() {
    while (running) {
        RecordingTask task;
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]{ return !running || !tasks.empty(); });
            if (!running && tasks.empty()) break;
            task = std::move(tasks.front());
            tasks.pop();
        }
        // process
        RecordingSession sessionCopy; // we will look up encoder/file under lock to ensure still valid
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = sessions.find(task.uid);
            if (it == sessions.end()) continue; // stopped meanwhile
            sessionCopy = it->second;
        }
        EncodeAndWrite(sessionCopy, task.pcm.data(), task.pcm.size(), task.sampleRate);
    }
}

void RecorderManager::EncodeAndWrite(RecordingSession& session, const int16_t* samples, size_t count, int sampleRate) {
    if (!session.encoder || !session.file || count == 0) return;
    // Encode in fixed frames (e.g., 20ms). 20ms at 24000Hz = 480 samples.
    const int frameSamples = sampleRate / 50; // 20ms
    std::vector<unsigned char> opusBuf(4000);
    size_t offset = 0;
    while (offset + frameSamples <= count) {
        int encoded = opus_encode(session.encoder, samples + offset, frameSamples, opusBuf.data(), (opus_int32)opusBuf.size());
        if (encoded > 0) {
            uint16_t sz = (uint16_t)encoded;
            fwrite(&sz, sizeof(uint16_t), 1, session.file); // length prefix
            fwrite(opusBuf.data(), 1, encoded, session.file);
            fflush(session.file);
        }
        offset += frameSamples;
    }
}

std::string RecorderManager::MakeFilename(int uid) const {
    auto t = std::time(nullptr);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << "recording_" << uid << "_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".opuspkt"; // custom container (length-prefixed packets)
    return oss.str();
}
