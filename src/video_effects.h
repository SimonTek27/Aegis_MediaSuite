// video_effects.h - GPU-accelerated video effects with audio-reactive features
// Integrates with audio platform for music-reactive visuals

#pragma once

#include "video_output.h"
#include <QObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>
#include <QJsonObject>
#include <QVariantMap>

namespace Aegis {

    // Forward declarations
    class AudioEngine;
    class EffectChain;

    // =============================================================================
    // Video Effect Base Class (GPU-accelerated)
    // =============================================================================

    class VideoEffect : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString id READ id CONSTANT)
        Q_PROPERTY(QString name READ name CONSTANT)
        Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
        Q_PROPERTY(bool needsAudioData READ needsAudioData CONSTANT)

    public:
        explicit VideoEffect(const QString& id, QObject* parent = nullptr);
        virtual ~VideoEffect();

        QString id() const { return m_id; }
        virtual QString name() const = 0;
        virtual QString category() const = 0;
        virtual QString description() const { return QString(); }

        // GPU Processing
        virtual void initializeGL();
        virtual void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                               const VideoPTS& pts) = 0;
                               virtual void cleanupGL();

                               // CPU fallback
                               virtual QImage processCPU(const QImage& input, const VideoPTS& pts);

                               // Parameters
                               virtual QVariantMap parameters() const;
                               virtual void setParameter(const QString& key, const QVariant& value);
                               virtual void setParameters(const QVariantMap& params);

                               // Keyframe animation support
                               void addKeyframe(const VideoPTS& pts, const QVariantMap& params);
                               void removeKeyframe(const VideoPTS& pts);
                               QVariantMap interpolateParameters(const VideoPTS& pts) const;

                               // Audio reactivity
                               virtual bool needsAudioData() const { return false; }
                               virtual void setAudioData(const QVector<float>& spectrum, float overallLevel);

                               // State
                               bool isEnabled() const { return m_enabled; }
                               void setEnabled(bool enabled);

                               // Serialization
                               virtual QJsonObject toJson() const;
                               virtual void fromJson(const QJsonObject& json);

    signals:
        void enabledChanged(bool enabled);
        void parameterChanged(const QString& key, const QVariant& value);
        void error(const QString& message);

    protected:
        QString m_id;
        bool m_enabled = true;
        QVariantMap m_parameters;
        QMap<qint64, QVariantMap> m_keyframes; // PTS -> params

        // Audio reactivity data
        QVector<float> m_audioSpectrum;
        float m_audioLevel = 0.0f;

        // OpenGL resources (lazy initialization)
        std::unique_ptr<QOpenGLShaderProgram> m_shaderProgram;
        bool m_glInitialized = false;
    };

    // =============================================================================
    // Effect Chain (Sequential Processing)
    // =============================================================================

    class VideoEffectChain : public QObject {
        Q_OBJECT
    public:
        explicit VideoEffectChain(QObject* parent = nullptr);
        ~VideoEffectChain();

        void addEffect(std::shared_ptr<VideoEffect> effect);
        void removeEffect(std::shared_ptr<VideoEffect> effect);
        void moveEffect(int fromIndex, int toIndex);
        void clear();

        QList<std::shared_ptr<VideoEffect>> effects() const;

        // Process frame through entire chain
        QOpenGLFramebufferObject* processChain(QOpenGLFramebufferObject* input, const VideoPTS& pts);

        // Audio data propagation
        void setAudioData(const QVector<float>& spectrum, float overallLevel);

        // Bypass all effects
        void setBypass(bool bypass) { m_bypass = bypass; }
        bool isBypassed() const { return m_bypass; }

    private:
        QList<std::shared_ptr<VideoEffect>> m_effects;
        std::vector<std::unique_ptr<QOpenGLFramebufferObject>> m_fboPool;
        bool m_bypass = false;

        QOpenGLFramebufferObject* getFBO(const QSize& size);
    };

    // =============================================================================
    // Built-in Video Effects
    // =============================================================================

    // Color Correction (Lift/Gamma/Gain)
    class ColorCorrectionEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit ColorCorrectionEffect(QObject* parent = nullptr);

        QString name() const override { return "Color Correction"; }
        QString category() const override { return "Color"; }
        QString description() const override {
            return "Professional color grading with lift, gamma, gain controls";
        }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       QVariantMap parameters() const override;
                       void setParameters(const QVariantMap& params) override;

                       // Color wheel controls
                       void setLift(float r, float g, float b);
                       void setGamma(float r, float g, float b);
                       void setGain(float r, float g, float b);
                       void setOffset(float r, float g, float b);
                       void setSaturation(float sat);
                       void setContrast(float con);
                       void setBrightness(float bri);
                       void setHue(float hue);

    private:
        float m_lift[3] = {0, 0, 0};
        float m_gamma[3] = {1, 1, 1};
        float m_gain[3] = {1, 1, 1};
        float m_offset[3] = {0, 0, 0};
        float m_saturation = 1.0f;
        float m_contrast = 1.0f;
        float m_brightness = 0.0f;
        float m_hue = 0.0f;
    };

    // Blur / Sharpen
    class BlurEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit BlurEffect(QObject* parent = nullptr);

        QString name() const override { return "Blur/Sharpen"; }
        QString category() const override { return "Blur"; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       void setRadius(float radius);  // Negative for sharpen
                       void setQuality(int quality);  // 0=fast, 1=quality

    private:
        float m_radius = 0.0f;
        int m_quality = 1;
    };

    // Audio-Reactive Visualizer (integrates with audio platform)
    class AudioVisualizerEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit AudioVisualizerEffect(QObject* parent = nullptr);

        QString name() const override { return "Audio Visualizer"; }
        QString category() const override { return "Audio Reactive"; }
        bool needsAudioData() const override { return true; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       // Visualizer types
                       enum VisualizerType {
                           Waveform,
                           Spectrum,
                           Oscilloscope,
                           Particles,
                           Circular
                       };
                       void setVisualizerType(VisualizerType type);
                       void setColor(const QColor& color);
                       void setSmoothing(float smoothing); // 0.0-1.0

    private:
        VisualizerType m_type = Spectrum;
        QColor m_color = Qt::cyan;
        float m_smoothing = 0.5f;
    };

    // Chroma Key (Green Screen)
    class ChromaKeyEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit ChromaKeyEffect(QObject* parent = nullptr);

        QString name() const override { return "Chroma Key"; }
        QString category() const override { return "Keying"; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       void setKeyColor(const QColor& color);
                       void setTolerance(float tolerance);
                       void setFeather(float feather);
                       void setSpillSuppression(float spill);

    private:
        QColor m_keyColor = Qt::green;
        float m_tolerance = 0.3f;
        float m_feather = 0.1f;
        float m_spillSuppression = 0.5f;
    };

    // LUT / Color Grading
    class LUTEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit LUTEffect(QObject* parent = nullptr);

        QString name() const override { return "LUT"; }
        QString category() const override { return "Color"; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       bool loadLUT(const QString& filePath);  // .cube format
                       void setIntensity(float intensity); // 0.0-1.0

    private:
        std::unique_ptr<QOpenGLTexture> m_lutTexture;
        float m_intensity = 1.0f;
    };

    // Distortion / Warp
    class DistortionEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit DistortionEffect(QObject* parent = nullptr);

        QString name() const override { return "Distortion"; }
        QString category() const override { return "Distort"; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       enum DistortionType {
                           Bulge,
                           Pinch,
                           Wave,
                           Ripple,
                           Fisheye
                       };
                       void setDistortionType(DistortionType type);
                       void setAmount(float amount);
                       void setCenter(const QPointF& center); // Normalized 0-1

    private:
        DistortionType m_type = Wave;
        float m_amount = 0.5f;
        QPointF m_center{0.5f, 0.5f};
    };

    // Transition / Dissolve
    class TransitionEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit TransitionEffect(QObject* parent = nullptr);

        QString name() const override { return "Transition"; }
        QString category() const override { return "Transition"; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       enum TransitionType {
                           Dissolve,
                           Wipe,
                           Slide,
                           Push,
                           FadeToColor,
                           CrossZoom
                       };
                       void setTransitionType(TransitionType type);
                       void setProgress(float progress); // 0.0-1.0
                       void setTargetFrame(QOpenGLFramebufferObject* target);

    private:
        TransitionType m_type = Dissolve;
        float m_progress = 0.0f;
        QOpenGLFramebufferObject* m_targetFrame = nullptr;
    };

    // Grain / Noise
    class GrainEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit GrainEffect(QObject* parent = nullptr);

        QString name() const override { return "Film Grain"; }
        QString category() const override { return "Film"; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       void setAmount(float amount);
                       void setSize(float size);

    private:
        float m_amount = 0.1f;
        float m_size = 1.0f;
        std::unique_ptr<QOpenGLTexture> m_noiseTexture;
    };

    // Vignette
    class VignetteEffect : public VideoEffect {
        Q_OBJECT
    public:
        explicit VignetteEffect(QObject* parent = nullptr);

        QString name() const override { return "Vignette"; }
        QString category() const override { return "Stylize"; }

        void processGL(QOpenGLFramebufferObject* input, QOpenGLFramebufferObject* output,
                       const VideoPTS& pts) override;

                       void setIntensity(float intensity);
                       void setSmoothness(float smoothness);
                       void setCenter(const QPointF& center);

    private:
        float m_intensity = 0.5f;
        float m_smoothness = 0.5f;
        QPointF m_center{0.5f, 0.5f};
    };

    // =============================================================================
    // Effect Factory
    // =============================================================================

    class VideoEffectFactory {
    public:
        static std::shared_ptr<VideoEffect> create(const QString& effectId);
        static QStringList availableEffects();
        static QHash<QString, QString> effectInfo(const QString& effectId);
    };

} // namespace Aegis

Q_DECLARE_METATYPE(Aegis::VideoEffect*)
Q_DECLARE_METATYPE(Aegis::VideoEffectChain*)
