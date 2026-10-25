#include "AudioEngine.h"
#include "miniaudio.h"
#include <QByteArray>
#include <QDebug>
#include <algorithm>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace {
size_t nextPow2(size_t v) { size_t p = 1; while (p < v) p <<= 1; return p; }
}

AudioEngine::AudioEngine(QObject* parent) : QObject(parent) {}

AudioEngine::~AudioEngine()
{
    haltThread();
    std::lock_guard<std::mutex> g(m_devMx);
    if (m_dev) { ma_device_uninit(m_dev); delete m_dev; m_dev = nullptr; }
}

// ---- control (UI thread) -------------------------------------------------------

void AudioEngine::haltThread()
{
    m_stop = true;
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void AudioEngine::stop()
{
    haltThread();
    std::lock_guard<std::mutex> g(m_mx);
    m_rd = m_wr;
    m_segs.clear();
    m_playing = false;
}

void AudioEngine::load(const QString& path, qint64 token, qint64 startMs, bool autoplay, const QString& headers)
{
    haltThread();
    {
        std::lock_guard<std::mutex> g(m_mx);
        m_rd = m_wr;
        m_segs.clear();
    }
    { std::lock_guard<std::mutex> g(m_jobMx); m_next = {}; m_cur = {path, token, startMs, true, headers}; }
    m_error.clear();
    m_stop = false;
    m_ended = false;
    m_seekReq = false;
    m_playing = autoplay;
    if (autoplay) deviceStart();
    m_thread = std::thread(&AudioEngine::decodeLoop, this, Job{path, token, startMs, true, headers});
}

void AudioEngine::setNext(const QString& path, qint64 token, const QString& headers)
{
    std::lock_guard<std::mutex> g(m_jobMx);
    m_next = path.isEmpty() ? Job{} : Job{path, token, 0, true, headers};
}

void AudioEngine::setPlaying(bool on)
{
    m_playing = on;
    if (on) deviceStart();
    else m_cv.notify_all();
}

void AudioEngine::seek(qint64 ms)
{
    Segment cur;
    {
        std::lock_guard<std::mutex> g(m_mx);
        if (m_segs.empty()) return;
        auto it = m_segs.begin();
        while (std::next(it) != m_segs.end() && std::next(it)->startFrame <= m_rd) ++it;
        cur = *it;
    }
    Job decoding;
    { std::lock_guard<std::mutex> g(m_jobMx); decoding = m_cur; }
    if (cur.token == decoding.token) {
        m_seekMs = ms;
        m_seekReq = true;
        m_cv.notify_all();
        return;
    }
    // The decoder already moved on to the next track (we're inside the
    // buffered tail of this one): restart this track and re-queue the other.
    const bool wasPlaying = m_playing;
    const Job again = decoding;
    load(cur.path, cur.token, ms, wasPlaying, cur.headers);
    setNext(again.path, again.token, again.headers);
}

AudioEngine::Poll AudioEngine::poll()
{
    Poll p;
    {
        std::lock_guard<std::mutex> g(m_mx);
        while (m_segs.size() >= 2 && m_segs[1].startFrame <= m_rd) m_segs.pop_front();
        if (!m_segs.empty()) {
            const Segment& s = m_segs.front();
            p.token = s.token;
            const quint64 frames = m_rd >= s.startFrame ? m_rd - s.startFrame : 0;
            p.posMs = s.baseMs + qint64(frames * 1000 / quint64(std::max(1, s.rate)));
        }
        p.error = m_error;
    }
    if (m_ended.exchange(false)) p.ended = true;
    return p;
}

// ---- device -----------------------------------------------------------------------

void AudioEngine::dataCallback(ma_device* dev, void* out, const void*, unsigned frames)
{
    static_cast<AudioEngine*>(dev->pUserData)->render(static_cast<float*>(out), frames);
}

void AudioEngine::render(float* out, unsigned frames)
{
    const int ch = m_devCh;
    const size_t want = size_t(frames) * ch;
    if (!m_playing.load(std::memory_order_relaxed)) { std::memset(out, 0, want * sizeof(float)); return; }

    size_t got = 0;
    {
        std::lock_guard<std::mutex> g(m_mx);
        size_t n = std::min<size_t>(frames, size_t(m_wr - m_rd));
        const size_t mask = m_cap - 1;
        size_t pos = size_t(m_rd) & mask;
        size_t first = std::min(n, m_cap - pos);
        std::memcpy(out, &m_ring[pos * ch], first * ch * sizeof(float));
        if (n > first) std::memcpy(out + first * ch, &m_ring[0], (n - first) * ch * sizeof(float));
        m_rd += n;
        got = n * ch;
    }
    if (got < want) std::memset(out + got, 0, (want - got) * sizeof(float));
    const float gain = m_gain.load(std::memory_order_relaxed);
    if (gain < 0.9999f)                      // at 100 % the stream is bit-transparent
        for (size_t i = 0; i < got; ++i) out[i] *= gain;
    m_cv.notify_one();
}

void AudioEngine::deviceStart()
{
    std::lock_guard<std::mutex> g(m_devMx);
    if (m_dev && !m_devRunning) { if (ma_device_start(m_dev) == MA_SUCCESS) m_devRunning = true; }
}

void AudioEngine::deviceStopIdle()
{
    std::lock_guard<std::mutex> g(m_devMx);
    if (m_dev && m_devRunning && !m_playing) { ma_device_stop(m_dev); m_devRunning = false; }
}

bool AudioEngine::ensureDevice(int rate, int ch)
{
    {
        std::lock_guard<std::mutex> g(m_devMx);
        if (m_dev && m_devRate == rate && m_devCh == ch) return true;
    }
    // Different format: let whatever is buffered finish first (gapless is
    // only possible when the format matches).
    {
        std::unique_lock<std::mutex> lk(m_mx);
        m_cv.wait(lk, [&] { return m_stop.load() || m_wr == m_rd; });
    }
    if (m_stop) return false;

    std::lock_guard<std::mutex> g(m_devMx);
    if (m_dev) { ma_device_uninit(m_dev); delete m_dev; m_dev = nullptr; m_devRunning = false; }
    {
        std::lock_guard<std::mutex> lk(m_mx);
        m_cap = nextPow2(size_t(rate));            // ~1 s of audio
        m_ring.assign(m_cap * ch, 0.0f);
    }
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = ma_uint32(ch);
    cfg.sampleRate = ma_uint32(rate);
    cfg.dataCallback = &AudioEngine::dataCallback;
    cfg.pUserData = this;
    cfg.performanceProfile = ma_performance_profile_conservative;
    cfg.periodSizeInMilliseconds = 20;
    cfg.pulse.pStreamNamePlayback = "Lumen";

    m_dev = new ma_device;
    m_devRate = rate; m_devCh = ch;
    if (ma_device_init(nullptr, &cfg, m_dev) != MA_SUCCESS) {
        delete m_dev; m_dev = nullptr; m_devRate = m_devCh = 0;
        std::lock_guard<std::mutex> lk(m_mx);
        m_error = QStringLiteral("Could not open an audio output device");
        return false;
    }
    if (m_playing) { if (ma_device_start(m_dev) == MA_SUCCESS) m_devRunning = true; }
    return true;
}

bool AudioEngine::writeFrames(const float* data, size_t frames)
{
    const int ch = m_devCh;
    while (frames && !m_stop && !m_seekReq) {
        std::unique_lock<std::mutex> lk(m_mx);
        size_t space = m_cap - size_t(m_wr - m_rd);
        if (space == 0) {
            m_cv.wait_for(lk, std::chrono::milliseconds(20));
            continue;
        }
        size_t n = std::min(space, frames);
        const size_t mask = m_cap - 1;
        size_t pos = size_t(m_wr) & mask;
        size_t first = std::min(n, m_cap - pos);
        std::memcpy(&m_ring[pos * ch], data, first * ch * sizeof(float));
        if (n > first) std::memcpy(&m_ring[0], data + first * ch, (n - first) * ch * sizeof(float));
        m_wr += n;
        data += n * ch;
        frames -= n;
    }
    return !m_stop;
}

// ---- decoder thread ----------------------------------------------------------------

void AudioEngine::decodeLoop(Job job)
{
    while (!m_stop) {
        { std::lock_guard<std::mutex> g(m_jobMx); m_cur = job; }
        decodeOne(job);
        if (m_stop) return;
        Job nxt;
        {
            std::lock_guard<std::mutex> g(m_jobMx);
            nxt = m_next;
            m_next = {};
        }
        if (!nxt.valid) break;
        job = nxt;
        emit nextConsumed(job.token);
    }
    // Queue exhausted: wait for the tail to be heard, then report it.
    {
        std::unique_lock<std::mutex> lk(m_mx);
        m_cv.wait(lk, [&] { return m_stop.load() || m_wr == m_rd; });
    }
    if (!m_stop) m_ended = true;
}

bool AudioEngine::decodeOne(const Job& job)
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool ok = false;

    auto cleanup = [&] {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
    };
    auto fail = [&](const char* msg) {
        std::lock_guard<std::mutex> g(m_mx);
        m_error = QString::fromUtf8(msg) + QStringLiteral(": ") + job.path;
        cleanup();
        return false;
    };

    const QByteArray p = job.path.toUtf8();
    AVDictionary* opts = nullptr;
    if (job.path.contains("://")) {          // streamed from an extension
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&opts, "rw_timeout", "15000000", 0);
        av_dict_set(&opts, "user_agent", "Lumen/0.2", 0);
        if (!job.headers.isEmpty()) av_dict_set(&opts, "headers", job.headers.toUtf8().constData(), 0);
    }
    const int openRc = avformat_open_input(&fmt, p.constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (openRc < 0) return fail("Cannot open file");
    if (avformat_find_stream_info(fmt, nullptr) < 0) return fail("Cannot read stream info");
    const AVCodec* codec = nullptr;
    const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (si < 0 || !codec) return fail("No audio stream");
    AVStream* st = fmt->streams[si];
    ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    if (avcodec_open2(ctx, codec, nullptr) < 0) return fail("Cannot open decoder");

    const int rate = ctx->sample_rate;
    int inCh = ctx->ch_layout.nb_channels;
    if (ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&ctx->ch_layout, inCh);
    const int outCh = std::clamp(inCh, 1, 2);
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, outCh);
    if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, rate, &ctx->ch_layout, ctx->sample_fmt, rate, 0, nullptr) < 0
        || swr_init(swr) < 0)
        return fail("Cannot set up sample conversion");

    if (!ensureDevice(rate, outCh)) { cleanup(); return false; }

    auto pushSegment = [&](qint64 baseMs) {
        std::lock_guard<std::mutex> g(m_mx);
        m_segs.push_back({job.token, job.path, baseMs, m_wr, rate, job.headers});
    };

    double skipUntil = -1.0;
    auto doSeek = [&](qint64 ms) {
        {
            std::lock_guard<std::mutex> g(m_mx);
            m_rd = m_wr;                // drop everything buffered
            m_segs.clear();
        }
        av_seek_frame(fmt, -1, ms * 1000, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(ctx);
        skipUntil = ms / 1000.0;
        pushSegment(ms);
    };

    if (job.startMs > 0) doSeek(job.startMs); else pushSegment(0);

    std::vector<float> out;
    bool draining = false;
    while (!m_stop) {
        if (m_seekReq.exchange(false)) {
            doSeek(m_seekMs);
            draining = false;
            m_ended = false;
        }
        int rc = 0;
        if (!draining) {
            rc = av_read_frame(fmt, pkt);
            if (rc >= 0) {
                if (pkt->stream_index == si) avcodec_send_packet(ctx, pkt);
                av_packet_unref(pkt);
            } else {
                avcodec_send_packet(ctx, nullptr);
                draining = true;
            }
        }
        bool gotAny = false;
        while (!m_stop && !m_seekReq) {
            const int r = avcodec_receive_frame(ctx, frame);
            if (r < 0) break;
            gotAny = true;
            int skip = 0;
            if (skipUntil >= 0 && frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                const double t0 = frame->best_effort_timestamp * av_q2d(st->time_base);
                const double t1 = t0 + double(frame->nb_samples) / rate;
                if (t1 <= skipUntil) { av_frame_unref(frame); continue; }
                if (t0 < skipUntil) skip = int((skipUntil - t0) * rate);
            }
            skipUntil = -1.0;
            out.resize(size_t(frame->nb_samples + 256) * outCh);
            uint8_t* optr = reinterpret_cast<uint8_t*>(out.data());
            const int n = swr_convert(swr, &optr, frame->nb_samples + 256,
                                      const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            av_frame_unref(frame);
            if (n > skip && !writeFrames(out.data() + size_t(skip) * outCh, size_t(n - skip))) break;
        }
        if (m_seekReq) continue;
        if (draining && !gotAny) { ok = true; break; }
    }
    cleanup();
    return ok;
}
