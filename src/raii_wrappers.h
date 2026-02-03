// raii_wrappers.h - Modern C++ wrappers for C libraries
#pragma once

#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <type_traits>
#include <mutex>
#include <QVector>
#include <QString>
#include <sndfile.h>
#include <mpv/client.h>
#include <ebur128.h>
#include <fftw3.h>
#include <cdio/cdio.h>
#include <cdio/paranoia/cdda.h>

namespace Aegis {

    // Generic C resource wrapper
    template<typename T, auto Deleter>
    using CUniquePtr = std::unique_ptr<T, std::integral_constant<decltype(Deleter), Deleter>>;

    // Specific library wrappers
    using SndFilePtr   = std::unique_ptr<SNDFILE, decltype(&sf_close)>;
    using MpvHandlePtr = std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)>;

    struct EburStateDeleter {
        void operator()(ebur128_state* p) const {
            if (p) ebur128_destroy(&p);
        }
    };
    using Ebur128Ptr = std::unique_ptr<ebur128_state, EburStateDeleter>;

    using CdIoPtr      = std::unique_ptr<CdIo_t, decltype(&cdio_destroy)>;
    using CddaDrivePtr = std::unique_ptr<cdrom_drive_t, decltype(&cdda_close)>;

    inline SndFilePtr makeSndFilePtr(SNDFILE* f) { return SndFilePtr(f, &sf_close); }
    inline MpvHandlePtr makeMpvHandlePtr(mpv_handle* m) { return MpvHandlePtr(m, &mpv_terminate_destroy); }
    inline Ebur128Ptr makeEbur128Ptr(ebur128_state* e) { return Ebur128Ptr(e); }

    // FFTW wrappers with plan management
    struct FftwResources {
        struct PlanCache {
            std::unordered_map<size_t, fftwf_plan> forward_plans;
            std::unordered_map<size_t, fftwf_plan> inverse_plans;
            std::mutex cache_mutex;

            fftwf_plan getForward(int size, float* in, fftwf_complex* out);
            fftwf_plan getInverse(int size, fftwf_complex* in, float* out);
            void clear();
            ~PlanCache();
        };

        static PlanCache& instance();
    };

    // Buffer snapshot types for SharedBuffer
    template<typename T>
    class ConstBufferSnapshot {
        const QVector<T>* m_data;
        std::shared_lock<std::shared_mutex> m_lock;
    public:
        ConstBufferSnapshot(const QVector<T>* d, std::shared_mutex& m)
        : m_data(d), m_lock(m) {}
        const QVector<T>* operator->() const { return m_data; }
        const QVector<T>& operator*() const { return *m_data; }
    };

    template<typename T>
    class WritableBuffer {
        QVector<T>* m_data;
        std::unique_lock<std::shared_mutex> m_lock;
    public:
        WritableBuffer(QVector<T>* d, std::shared_mutex& m)
        : m_data(d), m_lock(m) {}
        QVector<T>* operator->() { return m_data; }
        QVector<T>& operator*() { return *m_data; }
    };

    // Thread-safe buffer with copy-on-write
    template<typename T>
    class SharedBuffer {
        struct Buffer {
            QVector<T> data;
            std::atomic<int> ref_count{1};
            mutable std::shared_mutex mutex;
        };

        std::atomic<Buffer*> m_buffer{nullptr};

    public:
        SharedBuffer() = default;
        ~SharedBuffer() { reset(); }

        // Delete copy, allow move
        SharedBuffer(const SharedBuffer&) = delete;
        SharedBuffer& operator=(const SharedBuffer&) = delete;
        SharedBuffer(SharedBuffer&& other) noexcept : m_buffer(other.m_buffer.exchange(nullptr)) {}
        SharedBuffer& operator=(SharedBuffer&& other) noexcept {
            if (this != &other) {
                reset();
                m_buffer.store(other.m_buffer.exchange(nullptr));
            }
            return *this;
        }

        ConstBufferSnapshot<T> read() const {
            Buffer* buf = m_buffer.load();
            if (!buf) throw std::runtime_error("Buffer not initialized");
            return ConstBufferSnapshot<T>(&buf->data, buf->mutex);
        }

        WritableBuffer<T> acquireWrite() {
            Buffer* buf = m_buffer.load();
            if (!buf) throw std::runtime_error("Buffer not initialized");
            return WritableBuffer<T>(&buf->data, buf->mutex);
        }

        void reset() {
            Buffer* buf = m_buffer.exchange(nullptr);
            if (buf && --buf->ref_count == 0) {
                delete buf;
            }
        }
    };

    // RAII WAV file writer with exception safety
    class WavFileWriter {
        SndFilePtr m_file;
        SF_INFO m_info{};
        sf_count_t m_framesWritten = 0;

    public:
        WavFileWriter(const QString& path, int samplerate, int channels, int format = SF_FORMAT_WAV | SF_FORMAT_FLOAT)
        : m_file(nullptr, &sf_close) {
            m_info.samplerate = samplerate;
            m_info.channels = channels;
            m_info.format = format;

            if (!sf_format_check(&m_info)) {
                throw std::invalid_argument("Invalid audio format configuration");
            }

            SNDFILE* f = sf_open(path.toUtf8().constData(), SFM_WRITE, &m_info);
            if (!f) throw std::runtime_error("Failed to open file for writing");

            m_file.reset(f);
        }

        ~WavFileWriter() {
            if (m_file) sf_write_sync(m_file.get());
        }

        void write(const float* data, sf_count_t frames) {
            if (!m_file) throw std::runtime_error("File not open");
            sf_count_t written = sf_writef_float(m_file.get(), data, frames);
            m_framesWritten += written;
            if (written != frames) {
                throw std::runtime_error("Failed to write all frames");
            }
        }

        void flush() {
            if (m_file) sf_write_sync(m_file.get());
        }

        sf_count_t framesWritten() const { return m_framesWritten; }
    };

} // namespace Aegis
