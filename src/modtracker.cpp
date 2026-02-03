// modtracker.cpp - ModTracker implementation using audio pillar
#include "modtracker.h"
#include <QFileInfo>

namespace Aegis {

    ModTracker::ModTracker(AudioEngine* engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_playback(engine ? engine->tracker() : nullptr)
    {
        if (!m_playback) {
            qWarning() << "AudioEngine has no tracker playback support";
        }

        // Connect to playback signals
        if (m_playback) {
            connect(m_playback, &ModTrackerPlayback::positionChanged,
                    this, &ModTracker::syncPlaybackState);
            connect(m_playback, &ModTrackerPlayback::finished,
                    this, &ModTracker::playingChanged);
        }
    }

    ModTracker::~ModTracker() = default;

    bool ModTracker::load(const QString &path) {
        if (!m_playback) return false;

        // Use AudioEngine's tracker for loading
        if (!m_engine->loadTrackerModule(path)) {
            emit error("Failed to load tracker module");
            return false;
        }

        // Load into editable format as well
        // ... parse module into m_module ...

        emit moduleChanged();
        return true;
    }

    void ModTracker::play() {
        if (m_playback) {
            m_playback->play();

            // Apply effects chain if set
            if (m_effects && m_engine) {
                // Connect effects to audio engine's processing
            }

            emit playingChanged();
        }
    }

    void ModTracker::stop() {
        if (m_playback) {
            m_playback->stop();
            emit playingChanged();
        }
    }

    bool ModTracker::playing() const {
        return m_playback ? m_playback->isPlaying() : false;
    }

    void ModTracker::setEffectChain(std::shared_ptr<EffectChain> chain) {
        m_effects = chain;
        // Connect to audio engine's effect processing
    }

    void ModTracker::syncPlaybackState() {
        if (!m_playback) return;

        double pos = m_playback->position();
        // Convert to pattern/row
        // int pattern = ...;
        // int row = ...;
        // emit playbackPositionChanged(pattern, row);
    }

    // ... (editing methods unchanged) ...

} // namespace Aegis
