// Minimal stub implementation for audio_output.cpp
// Currently only implements AudioOutputFactory, with no concrete backends.

#include "audio_output.h"

#include <QtGlobal>

namespace Aegis {

std::unique_ptr<AudioOutput> AudioOutputFactory::create(OutputBackend preferred)
{
    Q_UNUSED(preferred);
    // No backends wired up yet; caller must provide its own AudioOutput.
    return nullptr;
}

bool AudioOutputFactory::isBackendAvailable(OutputBackend backend)
{
    Q_UNUSED(backend);
    return false;
}

QString AudioOutputFactory::backendName(OutputBackend backend)
{
    switch (backend) {
    case OutputBackend::Auto:         return QStringLiteral("Auto");
    case OutputBackend::PipeWire:     return QStringLiteral("PipeWire");
    case OutputBackend::QtMultimedia: return QStringLiteral("Qt Multimedia");
    case OutputBackend::ALSA:         return QStringLiteral("ALSA");
    }
    return QStringLiteral("Unknown");
}

} // namespace Aegis
