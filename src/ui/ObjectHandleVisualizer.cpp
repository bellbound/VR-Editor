#include "ObjectHandleVisualizer.h"
#include "../selection/VirtualRaycastManager.h"
#include "../selection/SelectionState.h"
#include "../EditModeStateManager.h"
#include "../FrameCallbackDispatcher.h"
#include "../log.h"
#include <unordered_set>
#include <unordered_map>
#include <fmt/format.h>

ObjectHandleVisualizer* ObjectHandleVisualizer::GetSingleton()
{
    static ObjectHandleVisualizer instance;
    return &instance;
}

void ObjectHandleVisualizer::Initialize()
{
    if (m_initialized) {
        spdlog::warn("ObjectHandleVisualizer already initialized");
        return;
    }

    // Register for edit-mode-only frame callbacks
    // 3DUI visuals are created lazily in EnsureVisualsCreated()
    FrameCallbackDispatcher::GetSingleton()->Register(this, true);

    m_initialized = true;
    spdlog::info("ObjectHandleVisualizer initialized (visuals deferred to first use)");
}

void ObjectHandleVisualizer::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    for (auto& slot : m_iconSlots) {
        if (slot.active && slot.element) {
            slot.element->SetScale(0.0f);
            slot.element->SetVisible(false);
        }
        slot.assignedFormId = 0;
        slot.active = false;
    }

    if (m_root) {
        m_root->SetVisible(false);
    }

    FrameCallbackDispatcher::GetSingleton()->Unregister(this);

    m_initialized = false;
    m_visualsCreated = false;
    m_root = nullptr;
    m_iconSlots.clear();
    spdlog::info("ObjectHandleVisualizer shutdown");
}

void ObjectHandleVisualizer::OnEditModeExit()
{
    // Hide all active icon slots
    for (auto& slot : m_iconSlots) {
        if (slot.active && slot.element) {
            slot.element->SetScale(0.0f);
            slot.element->SetVisible(false);
        }
        slot.assignedFormId = 0;
        slot.active = false;
    }

    if (m_root) {
        m_root->SetVisible(false);
    }

    spdlog::info("ObjectHandleVisualizer::OnEditModeExit - Visuals hidden");
}

void ObjectHandleVisualizer::OnEditModeEnter()
{
    // Re-show root only if light selection is enabled and visuals were already created
    if (m_visualsCreated && m_root && Selection::VirtualRaycastManager::IsLightSelectionEnabled()) {
        m_root->SetVisible(true);
        spdlog::info("ObjectHandleVisualizer::OnEditModeEnter - Root shown (light selection enabled)");
    }
}

void ObjectHandleVisualizer::EnsureVisualsCreated()
{
    if (m_visualsCreated) {
        return;
    }

    auto* api = P3DUI::GetInterface001();
    if (!api) {
        spdlog::error("ObjectHandleVisualizer: 3DUI interface not available");
        return;
    }

    P3DUI::RootConfig rootCfg = P3DUI::RootConfig::Default("object_handles", "VREditor");
    rootCfg.interactive = false;
    m_root = api->GetOrCreateRoot(rootCfg);

    if (!m_root) {
        spdlog::error("ObjectHandleVisualizer: Failed to create root");
        return;
    }

    m_root->SetVRAnchor(P3DUI::VRAnchorType::None);
    m_root->SetFacingMode(P3DUI::FacingMode::None);

    m_iconSlots.clear();
    m_iconSlots.reserve(kMaxVisibleIcons);
    for (int i = 0; i < kMaxVisibleIcons; ++i) {
        std::string elementId = fmt::format("obj_handle_{}", m_nextElementId++);
        P3DUI::ElementConfig cfg = P3DUI::ElementConfig::Default(elementId.c_str());
        cfg.texturePath = kDefaultTexture;
        cfg.scale = kIconScale;
        cfg.facingMode = P3DUI::FacingMode::YawOnly;
        cfg.smoothingFactor = kSmoothingFactor;

        auto* elem = api->CreateElement(cfg);
        if (!elem) {
            spdlog::error("ObjectHandleVisualizer: Failed to create element {}", i);
            continue;
        }

        m_root->AddChild(elem);
        elem->SetVisible(false);
        m_iconSlots.push_back({elem, 0, false, HandleState::Lit, RE::FormType::Light});
    }

    m_root->SetVisible(true);
    m_visualsCreated = true;
    spdlog::info("ObjectHandleVisualizer: Created 3DUI root with {} icon slots", m_iconSlots.size());
}

void ObjectHandleVisualizer::OnFrameUpdate(float deltaTime)
{
    auto* stateManager = EditModeStateManager::GetSingleton();
    if (!stateManager) {
        return;
    }

    // Bail early if light selection is toggled off
    if (!Selection::VirtualRaycastManager::IsLightSelectionEnabled()) {
        for (auto& slot : m_iconSlots) {
            if (slot.active) {
                slot.element->SetScale(0.0f);
                slot.element->SetVisible(false);
                slot.assignedFormId = 0;
                slot.active = false;
            }
        }
        return;
    }

    // Active during Selecting and RemotePlacement (for grabbed light tracking)
    bool isSelecting = stateManager->GetState() == EditModeState::Selecting;
    bool isPlacing = stateManager->GetState() == EditModeState::RemotePlacement;

    if (!isSelecting && !isPlacing) {
        // Hide all active icons when not in a relevant mode
        for (auto& slot : m_iconSlots) {
            if (slot.active) {
                slot.element->SetScale(0.0f);
                slot.element->SetVisible(false);
                slot.assignedFormId = 0;
                slot.active = false;
            }
        }
        return;
    }

    EnsureVisualsCreated();
    if (!m_visualsCreated) {
        return;
    }

    // Slot assignment/release runs at throttled rate (15 FPS)
    m_updateTimer += deltaTime;
    if (m_updateTimer >= 1.0f / kUpdatesPerSecond) {
        m_updateTimer = 0.0f;
        UpdateSlotAssignments();
    }

    // Position and visual state updates run every frame (important during RemotePlacement)
    UpdateActiveSlots();
}

void ObjectHandleVisualizer::UpdateSlotAssignments()
{
    // Gather all refs that should have a visible handle icon
    // Sources: VirtualRaycastManager visible refs + selected lights from SelectionState
    std::unordered_map<RE::FormID, RE::TESObjectREFR*> desiredRefs;

    // Source 1: Visible refs from proximity scan (adaptive radius around player)
    auto* virtualManager = Selection::VirtualRaycastManager::GetSingleton();
    const auto& visibleRefs = virtualManager->GetVisibleRefs();
    for (auto* ref : visibleRefs) {
        if (ref) {
            // Idle marker handles disabled for now — re-enable by removing this guard
            auto* baseObj = ref->GetBaseObject();
            if (baseObj && baseObj->GetFormType() == RE::FormType::IdleMarker) {
                continue;
            }
            desiredRefs[ref->GetFormID()] = ref;
        }
    }

    // Source 2: Selected lights (always visible regardless of ray direction)
    auto* selectionState = Selection::SelectionState::GetSingleton();
    for (const auto& info : selectionState->GetSelection()) {
        if (info.ref && Selection::VirtualRaycastManager::IsVirtualRaycastCandidate(info.ref)) {
            // Idle marker handles disabled for now — re-enable by removing this guard
            auto* baseObj = info.ref->GetBaseObject();
            if (baseObj && baseObj->GetFormType() == RE::FormType::IdleMarker) {
                continue;
            }
            desiredRefs[info.formId] = info.ref;
        }
    }

    // Release stale slots (assigned to refs no longer in desired set)
    for (int i = 0; i < static_cast<int>(m_iconSlots.size()); ++i) {
        auto& slot = m_iconSlots[i];
        if (slot.active && desiredRefs.find(slot.assignedFormId) == desiredRefs.end()) {
            ReleaseSlot(i);
        }
    }

    // Assign new refs that don't already have a slot
    for (auto& [formId, ref] : desiredRefs) {
        if (FindSlotByFormId(formId) >= 0) {
            continue;  // Already assigned
        }

        int freeIdx = FindFreeSlot();
        if (freeIdx < 0) {
            break;  // Pool exhausted
        }

        AssignSlot(freeIdx, ref);
    }
}

void ObjectHandleVisualizer::UpdateActiveSlots()
{
    auto* virtualManager = Selection::VirtualRaycastManager::GetSingleton();
    auto* selectionState = Selection::SelectionState::GetSingleton();

    RE::TESObjectREFR* hoveredRef = virtualManager->GetHoveredRef();
    RE::FormID hoveredFormId = hoveredRef ? hoveredRef->GetFormID() : 0;

    for (auto& slot : m_iconSlots) {
        if (!slot.active || !slot.element) {
            continue;
        }

        // Look up the ref for this slot to update its position
        RE::TESObjectREFR* ref = nullptr;

        // Check virtual raycast visible refs
        for (auto* vRef : virtualManager->GetVisibleRefs()) {
            if (vRef && vRef->GetFormID() == slot.assignedFormId) {
                ref = vRef;
                break;
            }
        }

        // Check selection state
        if (!ref) {
            for (const auto& info : selectionState->GetSelection()) {
                if (info.formId == slot.assignedFormId) {
                    ref = info.ref;
                    break;
                }
            }
        }

        if (!ref) {
            continue;  // Ref not found — will be cleaned up on next slot assignment
        }

        // Update position (important during RemotePlacement when objects are moving)
        RE::NiPoint3 pos = ref->GetPosition();
        slot.element->SetLocalPosition(pos.x, pos.y, pos.z);

        // Determine desired visual state (priority: selected > hovered > lit)
        HandleState desiredState = HandleState::Lit;
        if (selectionState->IsSelected(slot.assignedFormId)) {
            desiredState = HandleState::Selected;
        } else if (slot.assignedFormId == hoveredFormId) {
            desiredState = HandleState::Hovered;
        }

        // Swap element if state changed (destroy + recreate with correct texture)
        if (desiredState != slot.state) {
            SwapSlotTexture(slot, desiredState);
        }
    }
}

void ObjectHandleVisualizer::SwapSlotTexture(IconSlot& slot, HandleState newState)
{
    auto* api = P3DUI::GetInterface001();
    if (!api || !m_root || !slot.element) {
        return;
    }

    // Remember position before destroying
    RE::NiPoint3 pos;
    // We can't read position back from the element, so we read from the ref
    // But we just set it above in UpdateActiveSlots, so just recreate at same spot

    // Remove old element (destroys it)
    m_root->RemoveChild(slot.element);
    slot.element = nullptr;

    // Create new element with correct texture
    std::string elementId = fmt::format("obj_handle_{}", m_nextElementId++);
    P3DUI::ElementConfig cfg = P3DUI::ElementConfig::Default(elementId.c_str());
    cfg.texturePath = GetTextureForState(newState, slot.formType);
    cfg.scale = kIconScale;
    cfg.facingMode = P3DUI::FacingMode::YawOnly;
    cfg.smoothingFactor = kSmoothingFactor;

    auto* newElem = api->CreateElement(cfg);
    if (!newElem) {
        spdlog::error("ObjectHandleVisualizer: Failed to create replacement element");
        slot.active = false;
        slot.assignedFormId = 0;
        slot.state = HandleState::Lit;
        return;
    }

    m_root->AddChild(newElem);
    // Position will be set on next frame by UpdateActiveSlots
    newElem->SetVisible(true);
    newElem->SetScale(kIconScale);

    slot.element = newElem;
    slot.state = newState;
}

const char* ObjectHandleVisualizer::GetTextureForState(HandleState state, RE::FormType formType)
{
    if (formType == RE::FormType::IdleMarker) {
        switch (state) {
            case HandleState::Hovered:  return kIdlmHoveredTexture;
            case HandleState::Selected: return kIdlmSelectedTexture;
            default:                    return kIdlmDefaultTexture;
        }
    }
    switch (state) {
        case HandleState::Hovered:  return kHoveredTexture;
        case HandleState::Selected: return kSelectedTexture;
        default:                    return kDefaultTexture;
    }
}

int ObjectHandleVisualizer::FindFreeSlot() const
{
    for (int i = 0; i < static_cast<int>(m_iconSlots.size()); ++i) {
        if (!m_iconSlots[i].active) {
            return i;
        }
    }
    return -1;
}

int ObjectHandleVisualizer::FindSlotByFormId(RE::FormID formId) const
{
    for (int i = 0; i < static_cast<int>(m_iconSlots.size()); ++i) {
        if (m_iconSlots[i].active && m_iconSlots[i].assignedFormId == formId) {
            return i;
        }
    }
    return -1;
}

void ObjectHandleVisualizer::ReleaseSlot(int index)
{
    auto& slot = m_iconSlots[index];
    if (slot.element) {
        slot.element->SetScale(0.0f);
        slot.element->SetVisible(false);
    }
    slot.assignedFormId = 0;
    slot.active = false;
    slot.state = HandleState::Lit;
    // Keep formType as-is so AssignSlot can detect texture mismatches
}

void ObjectHandleVisualizer::AssignSlot(int index, RE::TESObjectREFR* ref)
{
    auto& slot = m_iconSlots[index];
    RE::NiPoint3 pos = ref->GetPosition();

    // Determine base object form type for icon selection
    auto* baseObj = ref->GetBaseObject();
    RE::FormType newFormType = baseObj ? baseObj->GetFormType() : RE::FormType::Light;

    // Swap texture if state isn't default or form type changed (element still has old texture)
    if (slot.state != HandleState::Lit || slot.formType != newFormType) {
        slot.formType = newFormType;
        SwapSlotTexture(slot, HandleState::Lit);
    } else {
        slot.formType = newFormType;
    }

    // hide → move → scale 0 → show → target scale (smooth pop-in)
    slot.element->SetVisible(false);
    slot.element->SetLocalPosition(pos.x, pos.y, pos.z);
    slot.element->SetScale(0.0f);
    slot.element->SetVisible(true);
    slot.element->SetScale(kIconScale);

    slot.assignedFormId = ref->GetFormID();
    slot.active = true;
    slot.state = HandleState::Lit;
}
