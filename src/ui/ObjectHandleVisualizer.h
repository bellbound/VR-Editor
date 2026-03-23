#pragma once

#include "../IFrameUpdateListener.h"
#include "../interfaces/ThreeDUIInterface001.h"
#include <RE/Skyrim.h>
#include <vector>

// ObjectHandleVisualizer: Renders floating icons at the positions of invisible objects
//
// Works in conjunction with VirtualRaycastManager and SelectionState.
// Active during both Selecting and RemotePlacement states.
//
// Selecting mode:
// - Shows icons for refs whose visibility sphere the ray intersects (from VirtualRaycastManager)
// - Also shows icons for any selected lights (from SelectionState)
// - Hovered and selected lights get the highlighted texture
//
// RemotePlacement mode:
// - Shows icons for selected/grabbed lights being moved
// - Updates positions every frame (not throttled) since objects are moving
// - All shown with highlighted texture
//
// 3DUI root and elements are created lazily on first use (not during Initialize).
// Slot reassignment uses: SetVisible(false) → move → SetVisible(true) to prevent glide artifacts.
//
class ObjectHandleVisualizer : public IFrameUpdateListener
{
public:
    static ObjectHandleVisualizer* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Configuration
    static constexpr float kUpdatesPerSecond = 15.0f;
    static constexpr int kMaxVisibleIcons = 32;
    static constexpr float kIconScale = 1.5f;
    static constexpr float kSmoothingFactor = 10.0f;
    static constexpr const char* kLitTexture = "textures\\VREditor\\light_lit.dds";
    static constexpr const char* kHoveredTexture = "textures\\VREditor\\light_hovered.dds";

private:
    ObjectHandleVisualizer() = default;
    ~ObjectHandleVisualizer() = default;
    ObjectHandleVisualizer(const ObjectHandleVisualizer&) = delete;
    ObjectHandleVisualizer& operator=(const ObjectHandleVisualizer&) = delete;

    struct IconSlot {
        P3DUI::Element* element = nullptr;
        RE::FormID assignedFormId = 0;   // 0 = unassigned/free
        bool active = false;             // true = currently visible at a position
    };

    // Lazily create 3DUI root and element pool on first use
    void EnsureVisualsCreated();

    // Throttled: diff visible/selected refs against slots, assign/release as needed
    void UpdateSlotAssignments();

    // Every frame: update positions and textures for active slots
    void UpdateActiveSlots();

    // Find a free (inactive) slot, returns index or -1 if pool exhausted
    int FindFreeSlot() const;

    // Find slot assigned to a given FormID, returns index or -1 if not found
    int FindSlotByFormId(RE::FormID formId) const;

    // Hide and recycle a slot
    void ReleaseSlot(int index);

    // Assign a slot to a ref at its world position
    void AssignSlot(int index, RE::TESObjectREFR* ref);

    bool m_initialized = false;
    bool m_visualsCreated = false;

    P3DUI::Root* m_root = nullptr;
    std::vector<IconSlot> m_iconSlots;
    float m_updateTimer = 0.0f;
};
