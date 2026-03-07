// raii_wrappers.h - Production-grade RAII wrappers with complete safety
#pragma once

#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <type_traits>
#include <mutex>
#include <expected>
#include <QVector>
#include <QString>
#include <QDebug>
#include <sndfile.h>
#include <mpv/client.h>
#include <ebur128.h>
#include <fftw3.h>
#include <cdio/cdio.h>
#include <cdio/paranoia/cdda.h>

namespace Aegis {

    // ============================================================================
    // Result Type for Error Handling (C++23 std::expected style)
    // ============================================================================

    template<typename T, typename E = QString>
    class Result {
        std::variant<T, E> m_value;

    public:
        Result(T&& value) : m_value(std::forward<T>(value)) {}
        Result(const T& value) : m_value(value) {}
        Result(const E& error) : m_value(error) {}

        // Factory methods (named constructors)
        static Result success(T&& value) { return Result(std::forward<T>(value)); }
        static Result success(const T& value) { return Result(value); }
        static Result error(const E& err) { return Result(err); }
        static Result error(E&& err) { Result r; r.m_value = std::move(err); return r; }

        bool isSuccess() const { return std::holds_alternative<T>(m_value); }
        bool isError() const { return std::holds_alternative<E>(m_value); }

        T&& value() && { return std::move(std::get<T>(m_value)); }
        const T& value() const& { return std::get<T>(m_value); }

        E&& error() && { return std::move(std::get<E>(m_value)); }
        const E& error() const& { return std::get<E>(m_value); }

        template<typename Func>
        auto andThen(Func&& f) const -> Result<decltype(f(std::declval<T>())), E> {
            if (isSuccess()) {
                return f(std::get<T>(m_value));
            }
            return std::get<E>(m_value);
        }

        template<typename Func>
        auto mapError(Func&& f) const -> Result<T, decltype(f(std::declval<E>()))> {
            if (isError()) {
                return f(std::get<E>(m_value));
            }
            return std::get<T>(m_value);
        }

        // onError: runs func(err) if this is an error, returns *this for chaining
        template<typename Func>
        const Result& onError(Func&& f) const {
            if (isError()) f(std::get<E>(m_value));
            return *this;
        }

    private:
        Result() : m_value(T{}) {}  // used by error() factory
    };

    // ============================================================================
    // Result<void> specialization
    // ============================================================================

    template<typename E>
    class Result<void, E> {
        std::optional<E> m_error;

    public:
        Result() = default;  // success
        explicit Result(const E& err) : m_error(err) {}
        explicit Result(E&& err) : m_error(std::move(err)) {}

        static Result success() { return Result{}; }
        static Result error(const E& err) { return Result(err); }
        static Result error(E&& err) { return Result(std::move(err)); }

        bool isSuccess() const { return !m_error.has_value(); }
        bool isError() const { return m_error.has_value(); }

        const E& error() const { return *m_error; }
        E& error() { return *m_error; }

        template<typename Func>
        const Result& onError(Func&& f) const {
            if (isError()) f(*m_error);
            return *this;
        }
    };

    // ============================================================================
    // Enhanced Generic C Resource Wrapper with Ownership Tracking
    // ============================================================================

    template<typename T, auto Deleter>
    class ResourceHandle {
        static_assert(std::is_invocable_v<decltype(Deleter), T*>,
                      "Deleter must be callable with T*");

    private:
        struct ControlBlock {
            T* ptr;
            std::atomic<int> refCount{1};
            std::shared_mutex mutex;

            explicit ControlBlock(T* p) : ptr(p) {}
            ~ControlBlock() { if (ptr) Deleter(ptr); }
        };

        ControlBlock* m_block{nullptr};

    public:
        // Constructors/Destructors
        explicit ResourceHandle(T* ptr = nullptr)
        : m_block(ptr ? new ControlBlock(ptr) : nullptr) {}

        ~ResourceHandle() { release(); }

        // Copy semantics (shared ownership)
        ResourceHandle(const ResourceHandle& other) noexcept
        : m_block(other.m_block) {
            if (m_block) m_block->refCount.fetch_add(1, std::memory_order_relaxed);
        }

        ResourceHandle& operator=(const ResourceHandle& other) noexcept {
            if (this != &other) {
                release();
                m_block = other.m_block;
                if (m_block) m_block->refCount.fetch_add(1, std::memory_order_relaxed);
            }
            return *this;
        }

        // Move semantics
        ResourceHandle(ResourceHandle&& other) noexcept
        : m_block(std::exchange(other.m_block, nullptr)) {}

        ResourceHandle& operator=(ResourceHandle&& other) noexcept {
            if (this != &other) {
                release();
                m_block = std::exchange(other.m_block, nullptr);
            }
            return *this;
        }

        // Access
        T* get() const { return m_block ? m_block->ptr : nullptr; }
        T* operator->() const { return get(); }
        explicit operator bool() const { return get() != nullptr; }

        // Thread-safe access with locking
        template<typename Func>
        auto withLock(Func&& f) const -> decltype(f(std::declval<T*>())) {
            if (!m_block) throw std::runtime_error("Resource not initialized");
            std::shared_lock lock(m_block->mutex);
            return f(m_block->ptr);
        }

        template<typename Func>
        auto withExclusiveLock(Func&& f) -> decltype(f(std::declval<T*>())) {
            if (!m_block) throw std::runtime_error("Resource not initialized");
            std::unique_lock lock(m_block->mutex);
            return f(m_block->ptr);
        }

    private:
        void release() {
            if (m_block && m_block->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete m_block;
            }
            m_block = nullptr;
        }
    };

    // ============================================================================
    // Specific Library Resource Handles
    // ============================================================================

    inline void mpvDeleter(mpv_handle* p) { if (p) mpv_terminate_destroy(p); }
    inline void sndFileDeleter(SNDFILE* p) { if (p) sf_close(p); }
    inline void cdioDeleter(CdIo_t* p) { if (p) cdio_destroy(p); }
    inline void cddaDeleter(cdrom_drive_t* p) { if (p) cdda_close(p); }
    inline void ebur128Deleter(ebur128_state* p) { if (p) ebur128_destroy(&p); }

    using MpvHandle = ResourceHandle<mpv_handle, mpvDeleter>;
    using SndFileHandle = ResourceHandle<SNDFILE, sndFileDeleter>;
    using CdIoHandle = ResourceHandle<CdIo_t, cdioDeleter>;
    using CddaHandle = ResourceHandle<cdrom_drive_t, cddaDeleter>;
    using Ebur128Handle = ResourceHandle<ebur128_state, ebur128Deleter>;

    // ============================================================================
    // FFTW3 Advanced Wrapper with Automatic Plan Management
    // ============================================================================

    class FftwPlan {
    public:
        enum class Direction { Forward, Inverse };

    private:
        struct PlanDeleter {
            void operator()(fftwf_plan p) const { if (p) fftwf_destroy_plan(p); }
        };

        using PlanPtr = std::unique_ptr<std::remove_pointer_t<fftwf_plan>, PlanDeleter>;

        PlanPtr m_plan;
        int m_size{0};
        Direction m_direction{Direction::Forward};

        static std::shared_mutex s_cacheMutex;
        static std::unordered_map<size_t, PlanPtr> s_forwardCache;
        static std::unordered_map<size_t, PlanPtr> s_inverseCache;

    public:
        FftwPlan() = default;

        static Result<FftwPlan> create(int size, Direction dir) {
            if (size <= 0 || (size & (size - 1)) != 0) {
                return Result<FftwPlan>::error("FFT size must be positive power of two");
            }

            // Check cache first
            {
                std::shared_lock lock(s_cacheMutex);
                auto& cache = (dir == Direction::Forward) ? s_forwardCache : s_inverseCache;
                auto it = cache.find(static_cast<size_t>(size));
                if (it != cache.end()) {
                    FftwPlan plan;
                    // NOTE: FFTW plans are not copyable. Each caller creates their own plan.
                    // Fall through to create a new plan below.
                    // (cache hit skipped to avoid invalid copy)
                }
                // Cache miss or invalidated: create new plan
            }

            // Create new plan
            float* in = static_cast<float*>(fftwf_malloc(sizeof(float) * size));
            fftwf_complex* out = static_cast<fftwf_complex*>(
                fftwf_malloc(sizeof(fftwf_complex) * (size/2 + 1)));

            fftwf_plan rawPlan = (dir == Direction::Forward)
            ? fftwf_plan_dft_r2c_1d(size, in, out, FFTW_ESTIMATE)
            : fftwf_plan_dft_c2r_1d(size, out, in, FFTW_ESTIMATE);

            fftwf_free(in);
            fftwf_free(out);

            if (!rawPlan) {
                return Result<FftwPlan>::error("Failed to create FFTW plan");
            }

            // Cache the plan
            {
                std::unique_lock lock(s_cacheMutex);
                auto& cache = (dir == Direction::Forward) ? s_forwardCache : s_inverseCache;
                cache[static_cast<size_t>(size)] = PlanPtr(rawPlan);
            }

            FftwPlan plan;
            plan.m_plan = PlanPtr(rawPlan);
            plan.m_size = size;
            plan.m_direction = dir;
            return Result<FftwPlan>::success(std::move(plan));
        }

        void execute(float* in, fftwf_complex* out) const {
            if (!m_plan || m_direction != Direction::Forward) return;
            fftwf_execute_dft_r2c(m_plan.get(), in, out);
        }

        void execute(fftwf_complex* in, float* out) const {
            if (!m_plan || m_direction != Direction::Inverse) return;
            fftwf_execute_dft_c2r(m_plan.get(), in, out);
        }

        int size() const { return m_size; }
        Direction direction() const { return m_direction; }
    };

    // ============================================================================
    // Thread-Safe Ring Buffer with Lock-Free Operations
    // ============================================================================

    template<typename T, size_t Capacity>
    class RingBuffer {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
        static constexpr size_t MASK = Capacity - 1;

        alignas(64) std::array<T, Capacity> m_buffer;
        alignas(64) std::atomic<size_t> m_readIndex{0};
        alignas(64) std::atomic<size_t> m_writeIndex{0};
        alignas(64) std::atomic<bool> m_overflow{false};

    public:
        RingBuffer() { m_buffer.fill(T{}); }

        size_t write(const T* data, size_t count) noexcept {
            size_t write = m_writeIndex.load(std::memory_order_relaxed);
            size_t read = m_readIndex.load(std::memory_order_acquire);

            size_t available = Capacity - (write - read);
            size_t toWrite = std::min(count, available);

            size_t firstPart = std::min(toWrite, Capacity - (write & MASK));
            std::copy_n(data, firstPart, &m_buffer[write & MASK]);

            if (toWrite > firstPart) {
                std::copy_n(data + firstPart, toWrite - firstPart, &m_buffer[0]);
            }

            m_writeIndex.store(write + toWrite, std::memory_order_release);
            m_overflow.store(toWrite < count, std::memory_order_relaxed);

            return toWrite;
        }

        size_t read(T* data, size_t maxCount) noexcept {
            size_t read = m_readIndex.load(std::memory_order_relaxed);
            size_t write = m_writeIndex.load(std::memory_order_acquire);

            size_t available = write - read;
            size_t toRead = std::min(maxCount, available);

            size_t firstPart = std::min(toRead, Capacity - (read & MASK));
            std::copy_n(&m_buffer[read & MASK], firstPart, data);

            if (toRead > firstPart) {
                std::copy_n(&m_buffer[0], toRead - firstPart, data + firstPart);
            }

            m_readIndex.store(read + toRead, std::memory_order_release);
            return toRead;
        }

        size_t available() const noexcept {
            return m_writeIndex.load(std::memory_order_acquire) -
            m_readIndex.load(std::memory_order_acquire);
        }

        bool overflow() const noexcept { return m_overflow.load(std::memory_order_relaxed); }
        void reset() noexcept {
            m_readIndex.store(0, std::memory_order_relaxed);
            m_writeIndex.store(0, std::memory_order_relaxed);
            m_overflow.store(false, std::memory_order_relaxed);
        }

        // Non-blocking peek without consuming
        size_t peek(T* data, size_t maxCount) const noexcept {
            size_t read = m_readIndex.load(std::memory_order_acquire);
            size_t write = m_writeIndex.load(std::memory_order_acquire);

            size_t available = write - read;
            size_t toPeek = std::min(maxCount, available);

            size_t firstPart = std::min(toPeek, Capacity - (read & MASK));
            std::copy_n(&m_buffer[read & MASK], firstPart, data);

            if (toPeek > firstPart) {
                std::copy_n(&m_buffer[0], toPeek - firstPart, data + firstPart);
            }

            return toPeek;
        }
    };

    // ============================================================================
    // Production-Grade WAV File Writer with Error Recovery
    // ============================================================================

    struct WavFileFormat {
        int sampleRate{48000};
        int channels{2};
        int format{SF_FORMAT_WAV | SF_FORMAT_FLOAT};
    };

    class WavFileWriter {
    private:
        SndFileHandle m_file;
        WavFileFormat m_format;
        sf_count_t m_framesWritten{0};
        QString m_path;
        bool m_syncOnWrite{false};

    public:
        // Constructor defined out-of-line to avoid CWG1905 issue with
        // aggregate default member initializers in default arguments.
        explicit WavFileWriter(const QString& path, WavFileFormat fmt = WavFileFormat{}, bool syncOnWrite = false);

        Result<void> write(const float* data, sf_count_t frames) noexcept {
            try {
                if (!m_file) {
                    return Result<void>::error("File not open");
                }

                sf_count_t written = sf_writef_float(m_file.get(), data, frames);
                if (written != frames) {
                    return Result<void>::error(QString("Write error: %1")
                    .arg(sf_strerror(m_file.get())));
                }

                m_framesWritten += written;

                if (m_syncOnWrite) {
                    sf_write_sync(m_file.get());
                }

                return Result<void>::success();
            } catch (const std::exception& e) {
                return Result<void>::error(QString("Exception: %1").arg(e.what()));
            }
        }

        Result<void> flush() noexcept {
            try {
                if (m_file) {
                    sf_write_sync(m_file.get());
                }
                return Result<void>::success();
            } catch (const std::exception& e) {
                return Result<void>::error(QString("Flush failed: %1").arg(e.what()));
            }
        }

        sf_count_t framesWritten() const noexcept { return m_framesWritten; }
        const WavFileFormat& format() const noexcept { return m_format; }
        const QString& path() const noexcept { return m_path; }
    };

    // Static member definitions
    inline std::shared_mutex FftwPlan::s_cacheMutex;
    inline std::unordered_map<size_t, FftwPlan::PlanPtr> FftwPlan::s_forwardCache;
    inline std::unordered_map<size_t, FftwPlan::PlanPtr> FftwPlan::s_inverseCache;


    // ── WavFileWriter constructor (out-of-line to fix default-arg/inner-class issue) ──
    inline WavFileWriter::WavFileWriter(const QString& path, WavFileFormat fmt, bool syncOnWrite)
        : m_format(fmt)
        , m_path(path)
        , m_syncOnWrite(syncOnWrite)
    {
        if (!sf_format_check(reinterpret_cast<SF_INFO*>(&m_format))) {
            throw std::invalid_argument("Invalid audio format configuration");
        }
        SF_INFO info{};
        info.samplerate = m_format.sampleRate;
        info.channels   = m_format.channels;
        info.format     = m_format.format;
        SNDFILE* f = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
        if (!f) {
            throw std::runtime_error(
                QString("Failed to open file: %1").arg(sf_strerror(nullptr)).toStdString());
        }
        m_file = SndFileHandle(f);
    }

} // namespace Aegis
