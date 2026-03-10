// ffmpeg_utils.h - FFmpeg integration for video editor
#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/timecode.h>
}

#include <QObject>
#include <QImage>
#include <QString>
#include <QMutex>
#include <memory>
#include <functional>

namespace Aegis {

// RAII wrappers for FFmpeg objects
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) {
            av_packet_free(&pkt);
        }
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) {
            sws_freeContext(ctx);
        }
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const {
        if (ctx) {
            swr_free(&ctx);
        }
    }
};

using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

// ============================================================================
// Media Info Structure
// ============================================================================

struct MediaInfo {
    QString path;
    double duration = 0.0;  // seconds
    int64_t durationFrames = 0;
    
    // Video
    bool hasVideo = false;
    int videoWidth = 0;
    int videoHeight = 0;
    double videoFps = 30.0;
    int64_t videoBitrate = 0;
    QString videoCodec;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    
    // Audio
    bool hasAudio = false;
    int audioChannels = 0;
    int audioSampleRate = 0;
    int64_t audioBitrate = 0;
    QString audioCodec;
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
    
    bool isValid() const {
        return hasVideo || hasAudio;
    }
    
    QString resolutionString() const {
        return QString("%1x%2@%3fps").arg(videoWidth).arg(videoHeight).arg(videoFps);
    }
};

// ============================================================================
// FFmpeg Frame Extractor
// ============================================================================

class FrameExtractor : public QObject {
    Q_OBJECT
public:
    explicit FrameExtractor(QObject *parent = nullptr);
    ~FrameExtractor() override;
    
    // Open media file
    bool open(const QString &path);
    void close();
    bool isOpen() const { return m_formatCtx != nullptr; }
    
    // Media info
    MediaInfo info() const { return m_info; }
    
    // Seek to position (in seconds)
    bool seek(double position);
    bool seekFrame(int64_t frame);
    
    // Extract frame as QImage
    QImage extractFrame(double position);
    QImage extractFrameAt(int64_t frame);
    
    // Extract frame at current position and advance
    QImage getNextFrame();
    
    // Get current position
    double currentPosition() const { return m_currentPosition; }
    int64_t currentFrame() const;
    
    // Generate thumbnail
    QImage thumbnail(double position, const QSize &size);
    
    // Batch extraction for waveform
    std::vector<float> extractAudioPeaks(double start, double duration, int samples);

signals:
    void error(const QString &message);
    void frameReady(const QImage &frame, double position);

private:
    bool initializeVideoDecoder();
    bool decodeVideoFrame();
    QImage convertToQImage(AVFrame *frame);
    
    AVFormatContextPtr m_formatCtx;
    AVCodecContextPtr m_videoCodecCtx;
    AVFramePtr m_frame;
    AVFramePtr m_rgbFrame;
    SwsContextPtr m_swsCtx;
    
    MediaInfo m_info;
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
    
    double m_currentPosition = 0.0;
    int64_t m_currentPts = AV_NOPTS_VALUE;
    
    mutable QMutex m_mutex;
};

// ============================================================================
// Video Encoder for Export
// ============================================================================

class VideoEncoder : public QObject {
    Q_OBJECT
public:
    struct EncodeSettings {
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int videoBitrate = 8000000;  // 8 Mbps
        QString videoCodec = "libx264";
        QString preset = "medium";
        QString tune = "film";
        
        int audioSampleRate = 48000;
        int audioChannels = 2;
        int audioBitrate = 192000;  // 192 kbps
        QString audioCodec = "aac";
        
        QString pixelFormat = "yuv420p";
    };
    
    explicit VideoEncoder(QObject *parent = nullptr);
    ~VideoEncoder() override;
    
    // Initialize encoder
    bool initialize(const QString &outputPath, const EncodeSettings &settings);
    void finalize();
    bool isEncoding() const { return m_encoding; }
    
    // Encode video frame
    bool encodeVideoFrame(const QImage &frame);
    bool encodeVideoFrame(AVFrame *frame);
    
    // Encode audio samples
    bool encodeAudioSamples(const float *samples, int sampleCount);
    
    // Current progress
    int64_t framesEncoded() const { return m_framesEncoded; }
    double encodingProgress(int64_t totalFrames) const;

signals:
    void error(const QString &message);
    void progress(int64_t framesEncoded, int64_t totalFrames);
    void finished();

private:
    bool writeVideoFrame(AVFrame *frame);
    bool writeAudioFrame(AVFrame *frame);
    AVFrame* allocateVideoFrame();
    AVFrame* allocateAudioFrame();
    
    AVFormatContextPtr m_formatCtx;
    AVCodecContextPtr m_videoCodecCtx;
    AVCodecContextPtr m_audioCodecCtx;
    SwsContextPtr m_swsCtx;
    SwrContextPtr m_swrCtx;
    
    AVStream *m_videoStream = nullptr;
    AVStream *m_audioStream = nullptr;
    
    EncodeSettings m_settings;
    bool m_encoding = false;
    int64_t m_framesEncoded = 0;
    int64_t m_nextPts = 0;
    int64_t m_audioNextPts = 0;
    
    AVFramePtr m_videoFrame;
    AVFramePtr m_audioFrame;
    AVFramePtr m_convertedFrame;
    
    mutable QMutex m_mutex;
};

// ============================================================================
// FFmpeg Utilities
// ============================================================================

class FFmpegUtils {
public:
    // Initialize FFmpeg (call once at startup)
    static void initialize();
    
    // Get media info without opening full decoder
    static MediaInfo probeMedia(const QString &path);
    
    // Convert QImage to AVFrame
    static AVFramePtr imageToAVFrame(const QImage &image);
    
    // Convert AVFrame to QImage
    static QImage avFrameToImage(AVFrame *frame);
    
    // Get error string
    static QString errorString(int errnum);
    
    // List supported codecs/formats
    static QStringList supportedVideoCodecs();
    static QStringList supportedAudioCodecs();
    static QStringList supportedFormats();
    
    // Codec name to AVCodecID
    static AVCodecID codecNameToId(const QString &name);
};

} // namespace Aegis
