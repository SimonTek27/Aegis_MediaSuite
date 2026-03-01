// audio_effects.cpp - Audio Effects Implementation
//
// Most effect logic lives in audio_effects.h (inline / header-only).
// This file provides the out-of-line implementations for EffectChain and
// any future non-inline methods, keeping the translation unit valid for
// the Qt MOC-generated code.

#include "audio_effects.h"
#include <QDebug>

namespace Aegis {

// ============================================================================
// EffectChain - out-of-line methods (only those that cannot be inline)
// ============================================================================
// All EffectChain methods are currently defined inline in the header.
// This section is intentionally left for future non-trivial implementations.

// ============================================================================
// AudioEffect - virtual destructor (required for correct polymorphic deletion)
// ============================================================================
// Defined in header via "virtual ~AudioEffect() = default;".
// No additional implementation needed here.

// ============================================================================
// SmoothedParameter - all inline in header.
// ============================================================================

// ============================================================================
// EnhancedAudioBuffer - all inline in header.
// ============================================================================

// ============================================================================
// DSPUtils::BiquadCoeffs - all inline in header.
// ============================================================================

// ============================================================================
// Effect classes (GainEffect, PanEffect, FilterEffect, CompressorEffect,
// NormalizeEffect, FadeEffect, ReverseEffect, SilenceEffect)
// - all methods are inline in the header.
// ============================================================================

} // namespace Aegis
