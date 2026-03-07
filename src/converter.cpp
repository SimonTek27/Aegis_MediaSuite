// converter.cpp
#include "converter.h"
#include "mpv_backend.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace Aegis {

    QString Converter::buildMpvArgs(const ConvertJob &job) {
        Q_UNUSED(job);
        // TODO: build proper mpv/ffmpeg command line from job
        return QString();
    }

    Converter::Converter(AudioEngine* engine, QObject *parent)
        : QObject(parent)
        , m_engine(engine)
    {
    }

    Converter::~Converter() = default;

    bool Converter::converting() const { return m_converting; }

    void Converter::cancel() {
        // TODO: implement cancellation
    }

    void Converter::convertFile(const QString &input, const QString &output,
                                ConvertPreset preset,
                                std::shared_ptr<EffectChain> effects) {
        ConvertJob job{input, output, preset, {}, effects};
        processJob(job);
    }

    void Converter::convertBatch(const QStringList &inputs, const QString &outputDir,
                                 ConvertPreset preset) {
        for (const auto &in : inputs) {
            QString out = outputDir + "/" + QFileInfo(in).completeBaseName() + ".mp4";
            convertFile(in, out, preset, nullptr);
        }
    }

    void Converter::processJob(const ConvertJob &job) {
        Q_UNUSED(job);
        m_converting = false;
        emit convertingChanged();
        emit conversionFinished(true, QStringLiteral("Conversion not implemented yet"));
    }

} // namespace Aegis
