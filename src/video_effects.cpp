// video_effects.cpp - GPU-accelerated video effects implementation
#include "video_effects.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QtMath>
#include <QRandomGenerator>

namespace Aegis {

    // =============================================================================
    // VideoEffect Base Implementation
    // =============================================================================

    VideoEffect::VideoEffect(const QString& id, QObject* parent)
    : QObject(parent)
    , m_id(id) {}

    VideoEffect::~VideoEffect() {
        cleanupGL();
    }

    void VideoEffect::initializeGL() {
        if (m_glInitialized) return;

        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (!ctx) {
            emit error("No OpenGL context available");
            return;
        }

        m_glInitialized = true;
    }

    void VideoEffect::cleanupGL() {
        m_shaderProgram.reset();
        m_glInitialized = false;
    }

    QImage VideoEffect::processCPU(const QImage& input, const VideoPTS& pts) {
        Q_UNUSED(pts)
        return input; // Default: passthrough
    }

    QVariantMap VideoEffect::parameters() const {
        return m_parameters;
    }

    void VideoEffect::setParameter(const QString& key, const QVariant& value) {
        m_parameters[key] = value;
        emit parameterChanged(key, value);
    }

    void VideoEffect::setParameters(const QVariantMap& params) {
        for (auto it = params.begin(); it != params.end(); ++it) {
            setParameter(it.key(), it.value());
        }
    }

    void VideoEffect::addKeyframe(const VideoPTS& pts, const QVariantMap& params) {
        m_keyframes[pts.pts] = params;
    }

    void VideoEffect::removeKeyframe(const VideoPTS& pts) {
        m_keyframes.remove(pts.pts);
    }

    QVariantMap VideoEffect::interpolateParameters(const VideoPTS& pts) const {
        if (m_keyframes.isEmpty()) return m_parameters;

        // Find surrounding keyframes
        auto it = m_keyframes.lowerBound(pts.pts);
        if (it == m_keyframes.begin()) return it.value();
        if (it == m_keyframes.end()) return (it - 1).value();

        auto next = it;
        auto prev = it - 1;

        // Linear interpolation
        qint64 diff = next.key() - prev.key();
        if (diff == 0) return prev.value();

        double t = static_cast<double>(pts.pts - prev.key()) / diff;

        QVariantMap result = prev.value();
        for (auto it = next.value().begin(); it != next.value().end(); ++it) {
            if (result.contains(it.key())) {
                QVariant p = result[it.key()];
                QVariant n = it.value();
                if (p.typeId() == QMetaType::Double || p.typeId() == QMetaType::Float) {
                    result[it.key()] = p.toDouble() + (n.toDouble() - p.toDouble()) * t;
                }
            }
        }

        return result;
    }

    void VideoEffect::setAudioData(const QVector<float>& spectrum, float overallLevel) {
        m_audioSpectrum = spectrum;
        m_audioLevel = overallLevel;
    }

    void VideoEffect::setEnabled(bool enabled) {
        if (m_enabled != enabled) {
            m_enabled = enabled;
            emit enabledChanged(enabled);
        }
    }

    QJsonObject VideoEffect::toJson() const {
        QJsonObject json;
        json["id"] = m_id;
        json["enabled"] = m_enabled;

        QJsonObject params;
        for (auto it = m_parameters.begin(); it != m_parameters.end(); ++it) {
            params[it.key()] = QJsonValue::fromVariant(it.value());
        }
        json["parameters"] = params;

        return json;
    }

    void VideoEffect::fromJson(const QJsonObject& json) {
        setEnabled(json["enabled"].toBool(true));

        QJsonObject params = json["parameters"].toObject();
        for (auto it = params.begin(); it != params.end(); ++it) {
            setParameter(it.key(), it.value().toVariant());
        }
    }

    // =============================================================================
    // VideoEffectChain Implementation
    // =============================================================================

    VideoEffectChain::VideoEffectChain(QObject* parent) : QObject(parent) {}

    VideoEffectChain::~VideoEffectChain() = default;

    void VideoEffectChain::addEffect(std::shared_ptr<VideoEffect> effect) {
        m_effects.append(effect);
    }

    void VideoEffectChain::removeEffect(std::shared_ptr<VideoEffect> effect) {
        m_effects.removeOne(effect);
    }

    void VideoEffectChain::moveEffect(int fromIndex, int toIndex) {
        if (fromIndex < 0 || fromIndex >= m_effects.size()) return;
        if (toIndex < 0 || toIndex >= m_effects.size()) return;

        m_effects.move(fromIndex, toIndex);
    }

    void VideoEffectChain::clear() {
        m_effects.clear();
    }

    QList<std::shared_ptr<VideoEffect>> VideoEffectChain::effects() const {
        return m_effects;
    }

    QOpenGLFramebufferObject* VideoEffectChain::processChain(QOpenGLFramebufferObject* input,
                                                             const VideoPTS& pts) {
        if (m_bypass || m_effects.isEmpty()) return input;

        QOpenGLFramebufferObject* current = input;
        QOpenGLFramebufferObject* next = nullptr;

        for (auto& effect : m_effects) {
            if (!effect->isEnabled()) continue;

            // Get output FBO
            next = getFBO(current->size());

            // Process
            effect->processGL(current, next, pts);

            // Swap
            if (current != input) {
                // Return intermediate FBOs to pool
            }
            current = next;
        }

        return current;
                                                             }

                                                             void VideoEffectChain::setAudioData(const QVector<float>& spectrum, float overallLevel) {
                                                                 for (auto& effect : m_effects) {
                                                                     if (effect->needsAudioData()) {
                                                                         effect->setAudioData(spectrum, overallLevel);
                                                                     }
                                                                 }
                                                             }

                                                             QOpenGLFramebufferObject* VideoEffectChain::getFBO(const QSize& size) {
                                                                 // Simple FBO pool implementation
                                                                 for (auto& fbo : m_fboPool) {
                                                                     if (fbo->size() == size) {
                                                                         return fbo.get();
                                                                     }
                                                                 }

                                                                 auto fbo = std::make_unique<QOpenGLFramebufferObject>(size);
                                                                 QOpenGLFramebufferObject* ptr = fbo.get();
                                                                 m_fboPool.push_back(std::move(fbo));
                                                                 return ptr;
                                                             }

                                                             // =============================================================================
                                                             // ColorCorrectionEffect Implementation
                                                             // =============================================================================

                                                             ColorCorrectionEffect::ColorCorrectionEffect(QObject* parent)
                                                             : VideoEffect("colorcorrection", parent) {}

                                                             void ColorCorrectionEffect::processGL(QOpenGLFramebufferObject* input,
                                                                                                   QOpenGLFramebufferObject* output,
                                                                                                   const VideoPTS& pts) {
                                                                 Q_UNUSED(pts)

                                                                 if (!m_shaderProgram) {
                                                                     const char* vs = R"(
            #version 330 core
            layout(location = 0) in vec2 position;
            layout(location = 1) in vec2 texCoord;
            out vec2 vTexCoord;
            void main() {
                gl_Position = vec4(position, 0.0, 1.0);
                vTexCoord = texCoord;
            }
        )";

        const char* fs = R"(
            #version 330 core
            in vec2 vTexCoord;
            out vec4 fragColor;
            uniform sampler2D inputTexture;
            uniform vec3 lift;
            uniform vec3 gamma;
            uniform vec3 gain;
            uniform vec3 offset;
            uniform float saturation;
            uniform float contrast;
            uniform float brightness;
            uniform float hue;

            vec3 rgbToHsv(vec3 rgb) {
                float maxC = max(max(rgb.r, rgb.g), rgb.b);
                float minC = min(min(rgb.r, rgb.g), rgb.b);
                float delta = maxC - minC;

                float h = 0.0;
                if (delta > 0.0) {
                    if (maxC == rgb.r) h = mod((rgb.g - rgb.b) / delta, 6.0);
                    else if (maxC == rgb.g) h = (rgb.b - rgb.r) / delta + 2.0;
                    else h = (rgb.r - rgb.g) / delta + 4.0;
                    h /= 6.0;
                }

                float s = maxC > 0.0 ? delta / maxC : 0.0;
                float v = maxC;

                return vec3(h, s, v);
            }

            vec3 hsvToRgb(vec3 hsv) {
                float h = hsv.x * 6.0;
                float s = hsv.y;
                float v = hsv.z;

                float c = v * s;
                float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
                float m = v - c;

                vec3 rgb;
                if (h < 1.0) rgb = vec3(c, x, 0.0);
                else if (h < 2.0) rgb = vec3(x, c, 0.0);
                else if (h < 3.0) rgb = vec3(0.0, c, x);
                else if (h < 4.0) rgb = vec3(0.0, x, c);
                else if (h < 5.0) rgb = vec3(x, 0.0, c);
                else rgb = vec3(c, 0.0, x);

                return rgb + m;
            }

            void main() {
                vec4 color = texture(inputTexture, vTexCoord);
                vec3 rgb = color.rgb;

                // Lift/Gamma/Gain (Log controls)
                rgb = rgb + lift * (1.0 - rgb);
                rgb = pow(rgb, 1.0 / gamma);
                rgb = rgb * gain;
                rgb = rgb + offset;

                // Contrast/Brightness
                rgb = (rgb - 0.5) * contrast + 0.5 + brightness;

                // Saturation
                vec3 hsv = rgbToHsv(rgb);
                hsv.y *= saturation;
                rgb = hsvToRgb(hsv);

                // Hue rotation
                if (hue != 0.0) {
                    hsv = rgbToHsv(rgb);
                    hsv.x = mod(hsv.x + hue, 1.0);
                    rgb = hsvToRgb(hsv);
                }

                fragColor = vec4(clamp(rgb, 0.0, 1.0), color.a);
            }
            )";

        m_shaderProgram = std::make_unique<QOpenGLShaderProgram>();
        m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
        m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
        m_shaderProgram->link();
                                                                 }

                                                                 output->bind();
                                                                 glClear(GL_COLOR_BUFFER_BIT);

                                                                 m_shaderProgram->bind();
                                                                 glActiveTexture(GL_TEXTURE0);
                                                                 glBindTexture(GL_TEXTURE_2D, input->texture());
                                                                 m_shaderProgram->setUniformValue("inputTexture", 0);

                                                                 m_shaderProgram->setUniformValue("lift", QVector3D(m_lift[0], m_lift[1], m_lift[2]));
                                                                 m_shaderProgram->setUniformValue("gamma", QVector3D(m_gamma[0], m_gamma[1], m_gamma[2]));
                                                                 m_shaderProgram->setUniformValue("gain", QVector3D(m_gain[0], m_gain[1], m_gain[2]));
                                                                 m_shaderProgram->setUniformValue("offset", QVector3D(m_offset[0], m_offset[1], m_offset[2]));
                                                                 m_shaderProgram->setUniformValue("saturation", m_saturation);
                                                                 m_shaderProgram->setUniformValue("contrast", m_contrast);
                                                                 m_shaderProgram->setUniformValue("brightness", m_brightness);
                                                                 m_shaderProgram->setUniformValue("hue", m_hue / 360.0f);

                                                                 // Render full-screen quad
                                                                 // (VAO setup would be here)

                                                                 m_shaderProgram->release();
                                                                 output->release();
                                                                                                   }

                                                                                                   QVariantMap ColorCorrectionEffect::parameters() const {
                                                                                                       QVariantMap params;
                                                                                                       params["liftR"] = m_lift[0];
                                                                                                       params["liftG"] = m_lift[1];
                                                                                                       params["liftB"] = m_lift[2];
                                                                                                       params["gammaR"] = m_gamma[0];
                                                                                                       params["gammaG"] = m_gamma[1];
                                                                                                       params["gammaB"] = m_gamma[2];
                                                                                                       params["gainR"] = m_gain[0];
                                                                                                       params["gainG"] = m_gain[1];
                                                                                                       params["gainB"] = m_gain[2];
                                                                                                       params["saturation"] = m_saturation;
                                                                                                       params["contrast"] = m_contrast;
                                                                                                       params["brightness"] = m_brightness;
                                                                                                       params["hue"] = m_hue;
                                                                                                       return params;
                                                                                                   }

                                                                                                   void ColorCorrectionEffect::setParameters(const QVariantMap& params) {
                                                                                                       if (params.contains("liftR")) m_lift[0] = params["liftR"].toFloat();
    if (params.contains("liftG")) m_lift[1] = params["liftG"].toFloat();
    if (params.contains("liftB")) m_lift[2] = params["liftB"].toFloat();
    if (params.contains("gammaR")) m_gamma[0] = params["gammaR"].toFloat();
    if (params.contains("gammaG")) m_gamma[1] = params["gammaG"].toFloat();
    if (params.contains("gammaB")) m_gamma[2] = params["gammaB"].toFloat();
    if (params.contains("gainR")) m_gain[0] = params["gainR"].toFloat();
    if (params.contains("gainG")) m_gain[1] = params["gainG"].toFloat();
    if (params.contains("gainB")) m_gain[2] = params["gainB"].toFloat();
    if (params.contains("saturation")) m_saturation = params["saturation"].toFloat();
    if (params.contains("contrast")) m_contrast = params["contrast"].toFloat();
    if (params.contains("brightness")) m_brightness = params["brightness"].toFloat();
    if (params.contains("hue")) m_hue = params["hue"].toFloat();
                                                                                                   }

void ColorCorrectionEffect::setLift(float r, float g, float b) {
    m_lift[0] = r; m_lift[1] = g; m_lift[2] = b;
}
void ColorCorrectionEffect::setGamma(float r, float g, float b) {
    m_gamma[0] = r; m_gamma[1] = g; m_gamma[2] = b;
}
void ColorCorrectionEffect::setGain(float r, float g, float b) {
    m_gain[0] = r; m_gain[1] = g; m_gain[2] = b;
}
void ColorCorrectionEffect::setOffset(float r, float g, float b) {
    m_offset[0] = r; m_offset[1] = g; m_offset[2] = b;
}
void ColorCorrectionEffect::setSaturation(float sat) { m_saturation = sat; }
void ColorCorrectionEffect::setContrast(float con) { m_contrast = con; }
void ColorCorrectionEffect::setBrightness(float bri) { m_brightness = bri; }
void ColorCorrectionEffect::setHue(float hue) { m_hue = hue; }

// =============================================================================
// AudioVisualizerEffect Implementation (Audio-Reactive)
// =============================================================================

AudioVisualizerEffect::AudioVisualizerEffect(QObject* parent)
: VideoEffect("audiovisualizer", parent) {}

void AudioVisualizerEffect::processGL(QOpenGLFramebufferObject* input,
                                      QOpenGLFramebufferObject* output,
                                      const VideoPTS& pts) {
    Q_UNUSED(pts)

    if (m_audioSpectrum.isEmpty()) {
        // No audio data, passthrough
        return;
    }

    if (!m_shaderProgram) {
        const char* vs = R"(
            #version 330 core
            layout(location = 0) in vec2 position;
            out float vAmplitude;
            uniform float spectrum[32];
            void main() {
                int idx = int(position.x * 32.0);
                idx = clamp(idx, 0, 31);
                vAmplitude = spectrum[idx];
                gl_Position = vec4(position.x * 2.0 - 1.0, vAmplitude - 0.5, 0.0, 1.0);
            }
        )";

        const char* fs = R"(
            #version 330 core
            in float vAmplitude;
            out vec4 fragColor;
            uniform vec3 color;
            void main() {
                fragColor = vec4(color * vAmplitude, 1.0);
            }
        )";

        m_shaderProgram = std::make_unique<QOpenGLShaderProgram>();
        m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
        m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
        m_shaderProgram->link();
    }

    output->bind();
    glClear(GL_COLOR_BUFFER_BIT);

    m_shaderProgram->bind();

    // Upload spectrum data
    QVector<float> smoothed = m_audioSpectrum;
    for (int i = 0; i < smoothed.size() && i < 32; i++) {
        m_shaderProgram->setUniformValue(QString("spectrum[%1]").arg(i).toUtf8().constData(),
                                         smoothed[i]);
    }

    m_shaderProgram->setUniformValue("color", QVector3D(m_color.redF(), m_color.greenF(),
                                                        m_color.blueF()));

    // Draw spectrum bars
    // (Geometry setup would be here)

    m_shaderProgram->release();
    output->release();
                                      }

                                      void AudioVisualizerEffect::setVisualizerType(VisualizerType type) { m_type = type; }
                                      void AudioVisualizerEffect::setColor(const QColor& color) { m_color = color; }
                                      void AudioVisualizerEffect::setSmoothing(float smoothing) { m_smoothing = smoothing; }

                                      // =============================================================================
                                      // VideoEffectFactory Implementation
                                      // =============================================================================

                                      std::shared_ptr<VideoEffect> VideoEffectFactory::create(const QString& effectId) {
                                          if (effectId == "colorcorrection") return std::make_shared<ColorCorrectionEffect>();
                                          if (effectId == "blur") return std::make_shared<BlurEffect>();
                                          if (effectId == "audiovisualizer") return std::make_shared<AudioVisualizerEffect>();
                                          if (effectId == "chromakey") return std::make_shared<ChromaKeyEffect>();
                                          if (effectId == "lut") return std::make_shared<LUTEffect>();
                                          if (effectId == "distortion") return std::make_shared<DistortionEffect>();
                                          if (effectId == "transition") return std::make_shared<TransitionEffect>();
                                          if (effectId == "grain") return std::make_shared<GrainEffect>();
                                          if (effectId == "vignette") return std::make_shared<VignetteEffect>();
                                          return nullptr;
                                      }

                                      QStringList VideoEffectFactory::availableEffects() {
                                          return QStringList{
                                              "colorcorrection", "blur", "audiovisualizer", "chromakey",
                                              "lut", "distortion", "transition", "grain", "vignette"
                                          };
                                      }

                                      QHash<QString, QString> VideoEffectFactory::effectInfo(const QString& effectId) {
                                          QHash<QString, QString> info;
                                          info["id"] = effectId;

                                          auto effect = create(effectId);
                                          if (effect) {
                                              info["name"] = effect->name();
                                              info["category"] = effect->category();
                                              info["description"] = effect->description();
                                          }

                                          return info;
                                      }

} // namespace Aegis
