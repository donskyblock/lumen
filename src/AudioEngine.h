#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

struct ma_device;

// Decode thread (FFmpeg) -> ring buffer of interleaved float32 -> miniaudio.
//
//  * The device is opened at the *source* sample rate, so nothing is resampled
//    by us. float32 holds 16/24-bit PCM exactly, and at gain == 1.0 the samples
//    are passed through untouched.
//  * Consecutive tracks with the same rate/channel count play gaplessly; a
//    different format drains the buffer and reopens the device (tiny gap).
class AudioEngine : public QObject {
    Q_OBJECT
public:
    struct Poll {
        qint64 token = -1;   // which queued track is audible right now
        qint64 posMs = 0;
        bool ended = false;  // queue ran dry (reported once)
        QString error;
        qint64 failedToken = -1;   // a queued track could not be opened / died mid-stream (reported once)
    };

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    void load(const QString& path, qint64 token, qint64 startMs = 0, bool autoplay = true, const QString& headers = {});
    void setNext(const QString& path, qint64 token, const QString& headers = {});   // empty path clears
    void setPlaying(bool on);
    bool playing() const { return m_playing; }
    void seek(qint64 ms);
    void setGain(float g) { m_gain = g; }
    void stop();
    void suspendIfPaused() { deviceStopIdle(); }   // release the sound card while paused
    Poll poll();

signals:
    void nextConsumed(qint64 token);   // decoder started the queued track

private:
    struct Job { QString path; qint64 token = -1; qint64 startMs = 0; bool valid = false; QString headers; };
    struct Segment { qint64 token; QString path; qint64 baseMs; quint64 startFrame; int rate; QString headers; };

    static void dataCallback(ma_device* dev, void* out, const void* in, unsigned frames);
    void render(float* out, unsigned frames);

    void decodeLoop(Job first);
    bool decodeOne(const Job& job);
    bool ensureDevice(int rate, int channels);
    bool writeFrames(const float* data, size_t frames);
    void haltThread();
    void deviceStart();
    void deviceStopIdle();

    // device (guarded by m_devMx)
    std::mutex m_devMx;
    ma_device* m_dev = nullptr;
    bool m_devRunning = false;
    int m_devRate = 0, m_devCh = 0;

    // ring buffer (guarded by m_mx). rd/wr are absolute frame counters.
    std::mutex m_mx;
    std::condition_variable m_cv;
    std::vector<float> m_ring;
    size_t m_cap = 0;
    quint64 m_rd = 0, m_wr = 0;
    std::deque<Segment> m_segs;
    qint64 m_failTok = -1;          // token of the track that failed, until poll() reports it (guarded by m_mx)

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_ended{false};
    std::atomic<bool> m_seekReq{false};
    std::atomic<qint64> m_seekMs{0};
    std::atomic<float> m_gain{1.0f};

    std::mutex m_jobMx;
    Job m_next;
    Job m_cur;
    QString m_error;
};
