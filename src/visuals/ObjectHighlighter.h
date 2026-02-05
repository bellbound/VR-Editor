#pragma once

#include "RE/Skyrim.h"
#include <unordered_map>

namespace ObjectHighlighter {

// Effect shader types for different highlight states
enum class HighlightType {
    Selection,  // EnchGreenFXShader - for selected objects
    Hover       // EnchBlueFXShader - for hovered objects
};

// Tracks an active effect on a reference
struct ActiveEffect {
    RE::TESObjectREFR* ref;
    RE::ShaderReferenceEffect* effect;
    HighlightType type;
};

// Initialize the highlighter system (caches effect shader forms, registers frame listener)
void Initialize();

// Shutdown and cleanup
void Shutdown();

// Process pending highlights (called internally via frame listener, exposed for testing)
void Update(float deltaTime);

// Highlight a reference with an effect shader
// Returns true if successfully highlighted
bool Highlight(RE::TESObjectREFR* ref, HighlightType type = HighlightType::Selection);

// Remove highlight from a reference
void Unhighlight(RE::TESObjectREFR* ref);

// Remove highlight by FormID (preferred - handles cases where ref pointer may have changed)
void UnhighlightByFormId(RE::FormID formId);

// Remove all highlights
void UnhighlightAll();

// Check if a reference is currently highlighted
bool IsHighlighted(RE::TESObjectREFR* ref);
bool IsHighlightedByFormId(RE::FormID formId);

// Legacy compatibility - these now ignore the NiAVObject* and config,
// using the formId to find the ref and apply effect shader
struct HighlightConfig {
    RE::NiColor color{0.0f, 1.0f, 0.5f};
    float emissiveMult = 2.0f;
};

bool Highlight(RE::NiAVObject* target, const HighlightConfig& config, RE::FormID formId);
void Unhighlight(RE::NiAVObject* target);

} // namespace ObjectHighlighter
