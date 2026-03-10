// ffmpeg_utils.cpp - FFmpeg integration implementation

#include "ffmpeg_utils.h"
#include <QDebug>
#include <QFileInfo>
#include <cmath>

namespace Aegis {

// ============================================================================
// FFmpeg Initialization
// ============================================================================

void FFmpegUtils::initialize() {
    static bool initialized = false;
    if (!initialized) {
        avformat_network_init();
        initialized = true;
    }
}

QString FFmpegUtils::errorString(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return QString::fromUtf8(errbuf);
}

MediaInfo FFmpegUtils::probeMedia(const QString &path) {
    FFmpegUtils::initialize();
    
    MediaInfo info;
    info.path = path;
    
    AVFormatContext *fmtCtx = nullptr;
    int ret = avformat_open_input(&fmtCtx, path.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        qWarning() << "Failed to open:" << path << errorString(ret);
        return info;
    }
    
    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        qWarning() << "Failed to find stream info:" << errorString(ret);
        avformat_close_input(&fmtCtx);
        return info;
    }
    
    // Duration
    if (fmtCtx->duration != AV_NOPTS_VALUE) {
        info.duration = fmtCtx->duration / (double)AV_TIME_BASE;
    }
    
    // Find video stream
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        AVStream *stream = fmtCtx->streams[i];
        const AVCodecParameters *codecpar = stream->codecpar;
        
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !info.hasVideo) {
            info.hasVideo = true;
            info.videoWidth = codecpar->width;
            info.videoHeight = codecpar->height;
            info.pixelFormat = (AVPixelFormat)codecpar->format;
            info.videoBitrate = codecpar->bit_rate;
            
            // FPS
            if (stream->avg_frame_rate.den && stream->avg_frame_rate.num) {
                info.videoFps = av_q2d(stream->avg_frame_rate);
            } else if (stream->r_frame_rate.den && stream->r_frame_rate.num) {
                info.videoFps = av_q2d(stream->r_frame_rate);
            }
            
            // Codec name
            const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
            if (codec) {
                info.videoCodec = QString::fromUtf8(codec->name);
            }
            
            info.durationFrames = static_cast<int64_t>(info.duration * info.videoFps);
        }
        else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !info.hasAudio) {
            info.hasAudio = true;
            info.audioChannels = codecpar->ch_layout.nb_channels;
            info.audioSampleRate = codecpar->sample_rate;
            info.audioBitrate = codecpar->bit_rate;
            info.sampleFormat = (AVSampleFormat)codecpar->format;
            
            const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
            if (codec) {
                info.audioCodec = QString::fromUtf8(codec->name);
            }
        }
    }
    
    avformat_close_input(&fmtCtx);
    return info;
}

QImage FFmpegUtils::avFrameToImage(AVFrame *frame) {
    if (!frame) return QImage();
    
    // Convert to RGB24 if needed
    SwsContext *swsCtx = sws_getContext(
        frame->width, frame->height, (AVPixelFormat)frame->format,
        frame->width, frame->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!swsCtx) return QImage();
    
    QImage image(frame->width, frame->height, QImage::Format_RGB888);
    uint8_t *dest[4] = { image.bits(), nullptr, nullptr, nullptr };
    int destLinesize[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };
    
    sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height, dest, destLinesize);
    sws_freeContext(swsCtx);
    
    return image;
}

AVFramePtr FFmpegUtils::imageToAVFrame(const QImage &image) {
    if (image.isNull()) return nullptr;
    
    AVFrame *frame = av_frame_alloc();
    if (!frame) return nullptr;
    
    frame->format = AV_PIX_FMT_RGB24;
    frame->width = image.width();
    frame->height = image.height();
    
    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        av_frame_free(&frame);
        return nullptr;
    }
    
    // Copy image data
    const uint8_t *src = image.bits();
    int srcLinesize = image.bytesPerLine();
    
    for (int y = 0; y < frame->height; y++) {
        memcpy(frame->data[0] + y * frame->linesize[0], 
               src + y * srcLinesize, 
               std::min(frame->linesize[0], srcLinesize));
    }
    
    return AVFramePtr(frame);
}

QStringList FFmpegUtils::supportedVideoCodecs() {
    QStringList codecs;
    const AVCodec *codec = nullptr;
    void *iter = nullptr;
    
    while ((codec = av_codec_iterate(&iter))) {
        if (codec->type == AVMEDIA_TYPE_VIDEO && av_codec_is_encoder(codec)) {
            codecs.append(QString::fromUtf8(codec->name));
        }
    }
    
    return codecs;
}

QStringList FFmpegUtils::supportedAudioCodecs() {
    QStringList codecs;
    const AVCodec *codec = nullptr;
    void *iter = nullptr;
    
    while ((codec = av_codec_iterate(&iter))) {
        if (codec->type == AVMEDIA_TYPE_AUDIO && av_codec_is_encoder(codec)) {
            codecs.append(QString::fromUtf8(codec->name));
        }
    }
    
    return codecs;
}

QStringList FFmpegUtils::supportedFormats() {
    QStringList formats;
    const AVOutputFormat *fmt = nullptr;
    void *iter = nullptr;
    
    while ((fmt = av_muxer_iterate(&iter))) {
        if (fmt->name) {
            formats.append(QString::fromUtf8(fmt->name));
        }
    }
    
    return formats;
}

AVCodecID FFmpegUtils::codecNameToId(const QString &name) {
    const AVCodec *codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
    return codec ? codec->id : AV_CODEC_ID_NONE;
}

// ============================================================================
// FrameExtractor Implementation
// ============================================================================

FrameExtractor::FrameExtractor(QObject *parent)
    : QObject(parent)
{
    FFmpegUtils::initialize();
}

FrameExtractor::~FrameExtractor() = default;

bool FrameExtractor::open(const QString &path) {
    QMutexLocker lock(&m_mutex);
    close();
    
    AVFormatContext *fmtCtx = nullptr;
    int ret = avformat_open_input(&fmtCtx, path.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        emit error("Failed to open: " + FFmpegUtils::errorString(ret));
        return false;
    }
    
    m_formatCtx.reset(fmtCtx);
    
    ret = avformat_find_stream_info(m_formatCtx.get(), nullptr);
    if (ret < 0) {
        emit error("Failed to find stream info: " + FFmpegUtils::errorString(ret));
        close();
        return false;
    }
    
    // Probe and store info
    m_info = FFmpegUtils::probeMedia(path);
    
    // Initialize video decoder
    if (!initializeVideoDecoder()) {
        close();
        return false;
    }
    
    // Allocate frames
    m_frame.reset(av_frame_alloc());
    m_rgbFrame.reset(av_frame_alloc());
    
    if (!m_frame || !m_rgbFrame) {
        emit error("Failed to allocate frames");
        close();
        return false;
    }
    
    // Allocate RGB buffer
    m_rgbFrame->format = AV_PIX_FMT_RGB24;
    m_rgbFrame->width = m_info.videoWidth;
    m_rgbFrame->height = m_info.videoHeight;
    
    ret = av_frame_get_buffer(m_rgbFrame.get(), 0);
    if (ret < 0) {
        emit error("Failed to allocate RGB buffer");
        close();
        return false;
    }
    
    // Initialize scaler
    m_swsCtx.reset(sws_getContext(
        m_info.videoWidth, m_info.videoHeight, m_info.pixelFormat,
        m_info.videoWidth, m_info.videoHeight, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    ));
    
    if (!m_swsCtx) {
        emit error("Failed to initialize scaler");
        close();
        return false;
    }
    
    m_currentPosition = 0.0;
    return true;
}

void FrameExtractor::close() {
    QMutexLocker lock(&m_mutex);
    
    m_swsCtx.reset();
    m_rgbFrame.reset();
    m_frame.reset();
    m_videoCodecCtx.reset();
    m_formatCtx.reset();
    
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_currentPosition = 0.0;
    m_currentPts = AV_NOPTS_VALUE;
}

bool FrameExtractor::initializeVideoDecoder() {
    // Find video stream
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            break;
        }
    }
    
    if (m_videoStreamIndex < 0) {
        emit error("No video stream found");
        return false;
    }
    
    AVStream *stream = m_formatCtx->streams[m_videoStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    
    if (!codec) {
        emit error("Codec not found");
        return false;
    }
    
    m_videoCodecCtx.reset(avcodec_alloc_context3(codec));
    if (!m_videoCodecCtx) {
        emit error("Failed to allocate codec context");
        return false;
    }
    
    int ret = avcodec_parameters_to_context(m_videoCodecCtx.get(), stream->codecpar);
    if (ret < 0) {
        emit error("Failed to copy codec parameters");
        return false;
    }
    
    ret = avcodec_open2(m_videoCodecCtx.get(), codec, nullptr);
    if (ret < 0) {
        emit error("Failed to open codec: " + FFmpegUtils::errorString(ret));
        return false;
    }
    
    return true;
}

bool FrameExtractor::seek(double position) {
    return seekFrame(static_cast<int64_t>(position * m_info.videoFps));
}

bool FrameExtractor::seekFrame(int64_t frame) {
    QMutexLocker lock(&m_mutex);
    
    if (!m_formatCtx || m_videoStreamIndex < 0) return false;
    
    AVStream *stream = m_formatCtx->streams[m_videoStreamIndex];
    int64_t targetPts = av_rescale_q(frame, 
        AVRational{1, static_cast<int>(m_info.videoFps)},
        stream->time_base);
    
    int ret = av_seek_frame(m_formatCtx.get(), m_videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        emit error("Seek failed: " + FFmpegUtils::errorString(ret));
        return false;
    }
    
    avcodec_flush_buffers(m_videoCodecCtx.get());
    m_currentPts = AV_NOPTS_VALUE;
    
    // Decode frames until we reach the target
    while (m_currentPts < targetPts) {
        if (!decodeVideoFrame()) {
            break;
        }
    }
    
    m_currentPosition = frame / m_info.videoFps;
    return true;
}

bool FrameExtractor::decodeVideoFrame() {
    AVPacketPtr packet(av_packet_alloc());
    
    while (av_read_frame(m_formatCtx.get(), packet.get()) >= 0) {
        if (packet->stream_index == m_videoStreamIndex) {
            int ret = avcodec_send_packet(m_videoCodecCtx.get(), packet.get());
            if (ret < 0) {
                av_packet_unref(packet.get());
                continue;
            }
            
            ret = avcodec_receive_frame(m_videoCodecCtx.get(), m_frame.get());
            av_packet_unref(packet.get());
            
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                continue;
            }
            if (ret < 0) {
                emit error("Decode error: " + FFmpegUtils::errorString(ret));
                return false;
            }
            
            m_currentPts = m_frame->pts;
            return true;
        }
        av_packet_unref(packet.get());
    }
    
    return false;
}

QImage FrameExtractor::extractFrame(double position) {
    if (!seek(position)) return QImage();
    return getNextFrame();
}

QImage FrameExtractor::extractFrameAt(int64_t frame) {
    if (!seekFrame(frame)) return QImage();
    return getNextFrame();
}

QImage FrameExtractor::getNextFrame() {
    QMutexLocker lock(&m_mutex);
    
    if (!m_frame || !m_swsCtx) return QImage();
    
    // Scale to RGB
    sws_scale(m_swsCtx.get(),
        m_frame->data, m_frame->linesize, 0, m_frame->height,
        m_rgbFrame->data, m_rgbFrame->linesize);
    
    // Create QImage
    QImage image(m_rgbFrame->width, m_rgbFrame->height, QImage::Format_RGB888);
    for (int y = 0; y < m_rgbFrame->height; y++) {
        memcpy(image.scanLine(y), m_rgbFrame->data[0] + y * m_rgbFrame->linesize[0], 
               m_rgbFrame->linesize[0]);
    }
    
    // Advance position
    if (m_info.videoFps > 0) {
        m_currentPosition += 1.0 / m_info.videoFps;
    }
    
    return image;
}

QImage FrameExtractor::thumbnail(double position, const QSize &size) {
    QImage frame = extractFrame(position);
    if (frame.isNull()) return QImage();
    
    return frame.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

int64_t FrameExtractor::currentFrame() const {
    return static_cast<int64_t>(m_currentPosition * m_info.videoFps);
}

// ============================================================================
// VideoEncoder Implementation
// ============================================================================

VideoEncoder::VideoEncoder(QObject *parent)
    : QObject(parent)
{
    FFmpegUtils::initialize();
}

VideoEncoder::~VideoEncoder() {
    finalize();
}

bool VideoEncoder::initialize(const QString &outputPath, const EncodeSettings &settings) {
    QMutexLocker lock(&m_mutex);
    
    if (m_encoding) {
        finalize();
    }
    
    m_settings = settings;
    
    // Allocate format context
    AVFormatContext *fmtCtx = nullptr;
    int ret = avformat_alloc_output_context2(&fmtCtx, nullptr, nullptr, 
                                              outputPath.toUtf8().constData());
    if (ret < 0 || !fmtCtx) {
        emit error("Failed to allocate output context");
        return false;
    }
    
    m_formatCtx.reset(fmtCtx);
    
    // Add video stream
    const AVCodec *videoCodec = avcodec_find_encoder_by_name(settings.videoCodec.toUtf8().constData());
    if (!videoCodec) {
        videoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    
    if (!videoCodec) {
        emit error("Video codec not found");
        return false;
    }
    
    m_videoStream = avformat_new_stream(m_formatCtx.get(), nullptr);
    if (!m_videoStream) {
        emit error("Failed to create video stream");
        return false;
    }
    
    m_videoCodecCtx.reset(avcodec_alloc_context3(videoCodec));
    if (!m_videoCodecCtx) {
        emit error("Failed to allocate video codec context");
        return false;
    }
    
    // Configure video codec
    m_videoCodecCtx->width = settings.width;
    m_videoCodecCtx->height = settings.height;
    m_videoCodecCtx->time_base = AVRational{1, settings.fps};
    m_videoCodecCtx->framerate = AVRational{settings.fps, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_videoCodecCtx->bit_rate = settings.videoBitrate;
    
    // Set codec options
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "preset", settings.preset.toUtf8().constData(), 0);
    if (!settings.tune.isEmpty()) {
        av_dict_set(&opts, "tune", settings.tune.toUtf8().constData(), 0);
    }
    
    ret = avcodec_open2(m_videoCodecCtx.get(), videoCodec, &opts);
    av_dict_free(&opts);
    
    if (ret < 0) {
        emit error("Failed to open video codec: " + FFmpegUtils::errorString(ret));
        return false;
    }
    
    ret = avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecCtx.get());
    if (ret < 0) {
        emit error("Failed to copy video codec parameters");
        return false;
    }
    
    m_videoStream->time_base = m_videoCodecCtx->time_base;
    
    // Open output file
    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_formatCtx->pb, outputPath.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            emit error("Failed to open output file: " + FFmpegUtils::errorString(ret));
            return false;
        }
    }
    
    // Write header
    ret = avformat_write_header(m_formatCtx.get(), nullptr);
    if (ret < 0) {
        emit error("Failed to write header: " + FFmpegUtils::errorString(ret));
        return false;
    }
    
    // Allocate video frame
    m_videoFrame.reset(av_frame_alloc());
    m_videoFrame->format = m_videoCodecCtx->pix_fmt;
    m_videoFrame->width = m_videoCodecCtx->width;
    m_videoFrame->height = m_videoCodecCtx->height;
    av_frame_get_buffer(m_videoFrame.get(), 0);

    // Initialize scaler for input conversion
    m_swsCtx.reset(sws_getContext(
        settings.width, settings.height, AV_PIX_FMT_RGB24,
        settings.width, settings.height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    ));

    // ── Audio stream setup ────────────────────────────────────────────────────
    const AVCodec *audioCodec = avcodec_find_encoder_by_name(
                                    settings.audioCodec.toUtf8().constData());
    if (!audioCodec)
        audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);

    if (audioCodec) {
        m_audioStream = avformat_new_stream(m_formatCtx.get(), nullptr);
        if (!m_audioStream) {
            emit error("Failed to create audio stream");
            return false;
        }

        m_audioCodecCtx.reset(avcodec_alloc_context3(audioCodec));
        if (!m_audioCodecCtx) {
            emit error("Failed to allocate audio codec context");
            return false;
        }

        m_audioCodecCtx->sample_rate    = settings.audioSampleRate;
        m_audioCodecCtx->bit_rate       = settings.audioBitrate;
        // avcodec_get_supported_config replaces deprecated AVCodec::sample_fmts (FFmpeg >= 6.1)
        {
            const AVSampleFormat* fmts = nullptr;
            int numFmts = 0;
            if (avcodec_get_supported_config(nullptr, audioCodec, AV_CODEC_CONFIG_SAMPLE_FORMAT,
                                             0, (const void**)&fmts, &numFmts) == 0
                && numFmts > 0) {
                m_audioCodecCtx->sample_fmt = fmts[0];
            } else {
                m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
            }
        }
        av_channel_layout_default(&m_audioCodecCtx->ch_layout,
                                   settings.audioChannels);

        if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
            m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        ret = avcodec_open2(m_audioCodecCtx.get(), audioCodec, nullptr);
        if (ret < 0) {
            emit error("Failed to open audio codec: " + FFmpegUtils::errorString(ret));
            return false;
        }

        ret = avcodec_parameters_from_context(m_audioStream->codecpar,
                                               m_audioCodecCtx.get());
        if (ret < 0) {
            emit error("Failed to copy audio codec parameters");
            return false;
        }
        m_audioStream->time_base = AVRational{1, m_audioCodecCtx->sample_rate};

        // Resampler: planar float (engine output) → codec native format
        SwrContext *swrRaw = nullptr;
        ret = swr_alloc_set_opts2(&swrRaw,
            &m_audioCodecCtx->ch_layout, m_audioCodecCtx->sample_fmt,
                                          m_audioCodecCtx->sample_rate,
            &m_audioCodecCtx->ch_layout, AV_SAMPLE_FMT_FLTP,
                                          m_audioCodecCtx->sample_rate,
            0, nullptr);
        if (ret < 0 || !swrRaw) {
            emit error("Failed to create resampler");
            return false;
        }
        m_swrCtx.reset(swrRaw);
        if (swr_init(m_swrCtx.get()) < 0) {
            emit error("Failed to initialise resampler");
            return false;
        }

        // Audio frame sized to codec frame_size (or a sensible default)
        int frameSize = m_audioCodecCtx->frame_size > 0
                            ? m_audioCodecCtx->frame_size : 1024;

        m_audioFrame.reset(av_frame_alloc());
        m_audioFrame->format         = m_audioCodecCtx->sample_fmt;
        m_audioFrame->sample_rate    = m_audioCodecCtx->sample_rate;
        m_audioFrame->nb_samples     = frameSize;
        av_channel_layout_copy(&m_audioFrame->ch_layout,
                                &m_audioCodecCtx->ch_layout);
        av_frame_get_buffer(m_audioFrame.get(), 0);
    }
    // ── End audio setup ───────────────────────────────────────────────────────

    m_encoding = true;
    m_framesEncoded = 0;
    m_nextPts = 0;
    m_audioNextPts = 0;

    return true;
}

void VideoEncoder::finalize() {
    QMutexLocker lock(&m_mutex);

    if (!m_encoding) return;

    // Flush video encoder
    if (m_videoCodecCtx) {
        avcodec_send_frame(m_videoCodecCtx.get(), nullptr);

        AVPacketPtr packet(av_packet_alloc());
        while (avcodec_receive_packet(m_videoCodecCtx.get(), packet.get()) >= 0) {
            av_packet_rescale_ts(packet.get(),
                m_videoCodecCtx->time_base,
                m_videoStream->time_base);
            packet->stream_index = m_videoStream->index;
            av_interleaved_write_frame(m_formatCtx.get(), packet.get());
            av_packet_unref(packet.get());
        }
    }

    // Flush audio encoder
    if (m_audioCodecCtx && m_audioStream) {
        avcodec_send_frame(m_audioCodecCtx.get(), nullptr);

        AVPacketPtr packet(av_packet_alloc());
        while (avcodec_receive_packet(m_audioCodecCtx.get(), packet.get()) >= 0) {
            av_packet_rescale_ts(packet.get(),
                m_audioCodecCtx->time_base,
                m_audioStream->time_base);
            packet->stream_index = m_audioStream->index;
            av_interleaved_write_frame(m_formatCtx.get(), packet.get());
            av_packet_unref(packet.get());
        }
    }

    // Write trailer
    if (m_formatCtx) {
        av_write_trailer(m_formatCtx.get());

        if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_formatCtx->pb);
        }
    }

    // Cleanup
    m_swsCtx.reset();
    m_swrCtx.reset();
    m_videoFrame.reset();
    m_audioFrame.reset();
    m_convertedFrame.reset();
    m_videoCodecCtx.reset();
    m_audioCodecCtx.reset();
    m_videoStream = nullptr;
    m_audioStream = nullptr;
    m_formatCtx.reset();

    m_encoding = false;
    emit finished();
}

bool VideoEncoder::encodeVideoFrame(const QImage &image) {
    if (image.isNull()) return false;
    
    AVFramePtr rgbFrame = FFmpegUtils::imageToAVFrame(image);
    if (!rgbFrame) return false;
    
    return encodeVideoFrame(rgbFrame.get());
}

bool VideoEncoder::encodeVideoFrame(AVFrame *frame) {
    QMutexLocker lock(&m_mutex);
    
    if (!m_encoding || !frame) return false;
    
    // Convert to YUV420P
    sws_scale(m_swsCtx.get(),
        frame->data, frame->linesize, 0, frame->height,
        m_videoFrame->data, m_videoFrame->linesize);
    
    m_videoFrame->pts = m_nextPts++;
    
    return writeVideoFrame(m_videoFrame.get());
}

bool VideoEncoder::writeVideoFrame(AVFrame *frame) {
    int ret = avcodec_send_frame(m_videoCodecCtx.get(), frame);
    if (ret < 0) {
        emit error("Error sending frame: " + FFmpegUtils::errorString(ret));
        return false;
    }
    
    AVPacketPtr packet(av_packet_alloc());
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(m_videoCodecCtx.get(), packet.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            emit error("Error encoding: " + FFmpegUtils::errorString(ret));
            return false;
        }
        
        av_packet_rescale_ts(packet.get(), 
            m_videoCodecCtx->time_base, 
            m_videoStream->time_base);
        packet->stream_index = m_videoStream->index;
        
        ret = av_interleaved_write_frame(m_formatCtx.get(), packet.get());
        av_packet_unref(packet.get());
        
        if (ret < 0) {
            emit error("Error writing packet: " + FFmpegUtils::errorString(ret));
            return false;
        }
        
        m_framesEncoded++;
    }
    
    return true;
}

bool VideoEncoder::encodeAudioSamples(const float *samples, int sampleCount) {
    QMutexLocker lock(&m_mutex);

    if (!m_encoding || !m_audioCodecCtx || !m_audioStream || !m_swrCtx)
        return true;  // no audio stream configured - silently OK

    if (!samples || sampleCount <= 0)
        return false;

    const int channels  = m_audioCodecCtx->ch_layout.nb_channels;
    const int frameSize = m_audioFrame->nb_samples;

    // Build a temporary planar-float source frame for swr_convert
    AVFramePtr srcFrame(av_frame_alloc());
    srcFrame->format      = AV_SAMPLE_FMT_FLTP;
    srcFrame->sample_rate = m_audioCodecCtx->sample_rate;
    srcFrame->nb_samples  = sampleCount / channels;
    av_channel_layout_copy(&srcFrame->ch_layout, &m_audioCodecCtx->ch_layout);
    av_frame_get_buffer(srcFrame.get(), 0);

    // De-interleave: samples[L0,R0,L1,R1,...] -> plane[0][L...], plane[1][R...]
    for (int ch = 0; ch < channels; ++ch) {
        float *dst = reinterpret_cast<float*>(srcFrame->data[ch]);
        for (int i = 0; i < srcFrame->nb_samples; ++i)
            dst[i] = samples[i * channels + ch];
    }

    // Feed into the resampler FIFO
    int ret = swr_convert(m_swrCtx.get(), nullptr, 0,
                          const_cast<const uint8_t**>(srcFrame->data),
                          srcFrame->nb_samples);
    if (ret < 0) {
        emit error("swr_convert failed: " + FFmpegUtils::errorString(ret));
        return false;
    }

    // Drain complete frames from the resampler and submit to the encoder
    while (swr_get_delay(m_swrCtx.get(), m_audioCodecCtx->sample_rate) >= frameSize) {
        av_frame_make_writable(m_audioFrame.get());

        ret = swr_convert(m_swrCtx.get(),
                          m_audioFrame->data, frameSize,
                          nullptr, 0);
        if (ret < 0) {
            emit error("swr_convert (drain) failed: " + FFmpegUtils::errorString(ret));
            return false;
        }

        m_audioFrame->pts = av_rescale_q(m_audioNextPts,
                                          AVRational{1, m_audioCodecCtx->sample_rate},
                                          m_audioCodecCtx->time_base);
        m_audioNextPts += ret;

        if (!writeAudioFrame(m_audioFrame.get()))
            return false;
    }

    return true;
}

bool VideoEncoder::writeAudioFrame(AVFrame *frame) {
    // NOTE: m_mutex is already held by encodeAudioSamples / finalize.
    int ret = avcodec_send_frame(m_audioCodecCtx.get(), frame);
    if (ret < 0) {
        emit error("Error sending audio frame: " + FFmpegUtils::errorString(ret));
        return false;
    }

    AVPacketPtr packet(av_packet_alloc());
    while (ret >= 0) {
        ret = avcodec_receive_packet(m_audioCodecCtx.get(), packet.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            emit error("Error encoding audio: " + FFmpegUtils::errorString(ret));
            return false;
        }

        av_packet_rescale_ts(packet.get(),
            m_audioCodecCtx->time_base,
            m_audioStream->time_base);
        packet->stream_index = m_audioStream->index;

        ret = av_interleaved_write_frame(m_formatCtx.get(), packet.get());
        av_packet_unref(packet.get());

        if (ret < 0) {
            emit error("Error writing audio packet: " + FFmpegUtils::errorString(ret));
            return false;
        }
    }

    return true;
}

double VideoEncoder::encodingProgress(int64_t totalFrames) const {
    if (totalFrames <= 0) return 0.0;
    return static_cast<double>(m_framesEncoded) / totalFrames;
}

} // namespace Aegis
