#include "ObjectHighlighter.h"
#include "../log.h"

namespace ObjectHighlighter {

namespace {
    // Global state
    bool g_initialized = false;

    // Cached effect shaders
    RE::TESEffectShader* g_selectionShader = nullptr;  // VRBuilderSelectedShader
    RE::TESEffectShader* g_hoverShader = nullptr;      // VRBuilderHighlightShader

    // Track active effects by FormID
    std::unordered_map<RE::FormID, ActiveEffect> g_activeEffects;

    // Plugin and FormID for the effect shaders (VREditor.esp is ESL-flagged)
    constexpr const char* kShaderPluginName = "VREditor.esp";
    constexpr RE::FormID kSelectionShaderLocalFormId = 0x802;  // VRBuilderSelectedShader
    constexpr RE::FormID kHoverShaderLocalFormId = 0x801;      // VRBuilderHighlightShader

    RE::TESEffectShader* GetShaderForType(HighlightType type) {
        switch (type) {
            case HighlightType::Selection:
                return g_selectionShader;
            case HighlightType::Hover:
                return g_hoverShader;
            default:
                return g_selectionShader;
        }
    }

    void StopEffect(ActiveEffect& active) {
        // The stored effect pointer can become dangling if the game cleaned it up.
        // Instead of directly accessing the stored pointer, iterate through the
        // game's managed effects to find and stop ours safely.
        if (active.ref) {
            auto* processLists = RE::ProcessLists::GetSingleton();
            if (processLists) {
                RE::TESEffectShader* shader = GetShaderForType(active.type);
                RE::FormID targetFormId = active.ref->GetFormID();
                bool effectFound = false;

                processLists->ForEachShaderEffect([shader, targetFormId, &effectFound](RE::ShaderReferenceEffect* effect) {
                    if (effect && effect->effectData == shader) {
                        // Check if this effect is on our target reference
                        auto targetRef = effect->target.get();
                        if (targetRef && targetRef.get() && targetRef.get()->GetFormID() == targetFormId) {
                            effect->finished = true;
                            effectFound = true;
                        }
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });

                if (!effectFound) {
                    spdlog::warn("ObjectHighlighter: StopEffect could not find effect for {:08X} in ProcessLists (shader={:08X}, type={}). Effect may be stuck or already removed.",
                        targetFormId, shader ? shader->GetFormID() : 0, static_cast<int>(active.type));
                }
            }
        }
        active.effect = nullptr;
    }

} // anonymous namespace

void Initialize() {
    if (g_initialized) {
        spdlog::warn("ObjectHighlighter already initialized");
        return;
    }

    // Cache the effect shaders by plugin name + local FormID
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler) {
        g_selectionShader = dataHandler->LookupForm<RE::TESEffectShader>(kSelectionShaderLocalFormId, kShaderPluginName);
        g_hoverShader = dataHandler->LookupForm<RE::TESEffectShader>(kHoverShaderLocalFormId, kShaderPluginName);
    }

    if (!g_selectionShader) {
        spdlog::error("ObjectHighlighter: Failed to find selection shader ({}:{:03X})", kShaderPluginName, kSelectionShaderLocalFormId);
    }
    if (!g_hoverShader) {
        spdlog::error("ObjectHighlighter: Failed to find hover shader ({}:{:03X})", kShaderPluginName, kHoverShaderLocalFormId);
    }

    g_activeEffects.clear();
    g_initialized = true;
    spdlog::info("ObjectHighlighter initialized with effect shaders (selection={:08X}, hover={:08X})",
        g_selectionShader ? g_selectionShader->GetFormID() : 0,
        g_hoverShader ? g_hoverShader->GetFormID() : 0);
}

void Shutdown() {
    if (!g_initialized) return;

    UnhighlightAll();
    g_selectionShader = nullptr;
    g_hoverShader = nullptr;
    g_initialized = false;
    spdlog::info("ObjectHighlighter shutdown");
}

bool Highlight(RE::TESObjectREFR* ref, HighlightType type) {
    if (!ref) {
        spdlog::warn("ObjectHighlighter: Cannot highlight null reference");
        return false;
    }

    RE::FormID formId = ref->GetFormID();

    // Check if already highlighted
    auto it = g_activeEffects.find(formId);
    if (it != g_activeEffects.end()) {
        // If same type, nothing to do
        if (it->second.type == type) {
            spdlog::trace("ObjectHighlighter: {:08X} already highlighted with same type", formId);
            return true;
        }
        // Different type - stop old effect first
        StopEffect(it->second);
        g_activeEffects.erase(it);
    }

    RE::TESEffectShader* shader = GetShaderForType(type);
    if (!shader) {
        spdlog::error("ObjectHighlighter: No shader available for type {}", static_cast<int>(type));
        return false;
    }

    // Apply the effect shader with infinite duration (-1.0f)
    RE::ShaderReferenceEffect* effect = ref->ApplyEffectShader(shader, -1.0f);
    if (!effect) {
        spdlog::warn("ObjectHighlighter: ApplyEffectShader returned null for {:08X}", formId);
        return false;
    }

    // Track the active effect
    ActiveEffect active;
    active.ref = ref;
    active.effect = effect;
    active.type = type;
    g_activeEffects[formId] = active;

    spdlog::trace("ObjectHighlighter: Applied effect shader to {:08X} (type={})", formId, static_cast<int>(type));
    return true;
}

void Unhighlight(RE::TESObjectREFR* ref) {
    if (!ref) return;
    UnhighlightByFormId(ref->GetFormID());
}

void UnhighlightByFormId(RE::FormID formId) {
    if (formId == 0) return;

    auto it = g_activeEffects.find(formId);
    if (it == g_activeEffects.end()) {
        spdlog::trace("ObjectHighlighter: FormID {:08X} not found in active effects", formId);
        return;
    }

    StopEffect(it->second);
    g_activeEffects.erase(it);
    spdlog::trace("ObjectHighlighter: Unhighlighted {:08X}", formId);
}

void UnhighlightAll() {
    for (auto& [formId, active] : g_activeEffects) {
        StopEffect(active);
    }
    g_activeEffects.clear();
    spdlog::trace("ObjectHighlighter: Unhighlighted all objects");
}

bool IsHighlighted(RE::TESObjectREFR* ref) {
    return ref && IsHighlightedByFormId(ref->GetFormID());
}

bool IsHighlightedByFormId(RE::FormID formId) {
    return formId != 0 && g_activeEffects.contains(formId);
}

// Legacy compatibility functions - these bridge the old interface to the new one
bool Highlight(RE::NiAVObject* target, const HighlightConfig& config, RE::FormID formId) {
    (void)target;  // Not used in effect shader approach

    if (formId == 0) {
        spdlog::warn("ObjectHighlighter: Legacy Highlight called with formId=0, cannot proceed");
        return false;
    }

    // Look up the reference by FormID
    auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId);
    if (!ref) {
        spdlog::warn("ObjectHighlighter: Could not find reference for FormID {:08X}", formId);
        return false;
    }

    // Determine highlight type based on emissiveMult (hover uses 1.0, selection uses 2.0)
    HighlightType type = (config.emissiveMult < 1.5f) ? HighlightType::Hover : HighlightType::Selection;

    return Highlight(ref, type);
}

void Unhighlight(RE::NiAVObject* target) {
    (void)target;  // Cannot unhighlight by node pointer alone with effect shaders
    // Callers should use UnhighlightByFormId instead
    spdlog::trace("ObjectHighlighter: Legacy Unhighlight(NiAVObject*) called - use UnhighlightByFormId for reliable cleanup");
}

} // namespace ObjectHighlighter
