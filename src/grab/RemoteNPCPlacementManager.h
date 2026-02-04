#pragma once

#include <RE/Skyrim.h>

namespace Grab {

// Result returned by OnExit with the NPC's before/after position for undo recording
struct NPCMoveResult {
    RE::FormID formId;
    RE::NiPoint3 initialPosition;
    RE::NiPoint3 finalPosition;
};

// RemoteNPCPlacementManager: Handles NPC-specific placement logic for remote grab
//
// DESIGN:
// - Called by RemoteGrabController when the selected object is an NPC
// - Provides simplified NPC movement without rotation or persistence
// - NPCs are moved by directly setting their position (no collision manipulation needed)
// - Movement is temporary - NPC will resume their AI packages after edit mode exits
//
// This is NOT a separate state - RemoteGrabController stays in RemotePlacement mode
// and delegates NPC-specific logic to this manager.
//
// Differences from regular object placement:
// - No rotation support (NPCs handle their own facing)
// - No Disable/Enable cycle (NPCs don't need collision sync like statics)
// - No persistence (NPC changes are not exported to BOS INI files)
// - No multi-selection (one NPC at a time)
//
class RemoteNPCPlacementManager
{
public:
    static RemoteNPCPlacementManager* GetSingleton();

    // Check if the given reference is an NPC (and thus should use this manager)
    static bool IsNPC(RE::TESObjectREFR* ref);

    // Called when entering remote placement with an NPC
    // Returns true if successfully started NPC placement
    bool OnEnter(RE::Actor* npc, float initialDistance);

    // Called when exiting remote placement
    // Returns the NPC's initial and final position for undo recording
    // Returns nullopt if the NPC became invalid during placement
    std::optional<NPCMoveResult> OnExit();

    // Called each frame to update NPC position
    // centerPos: The calculated target position from RemoteGrabController
    void UpdatePosition(const RE::NiPoint3& centerPos);

    // Check if NPC placement is active
    bool IsActive() const { return m_isActive; }

    // Get the NPC being moved
    RE::Actor* GetNPC() const { return m_npc; }

private:
    RemoteNPCPlacementManager() = default;
    ~RemoteNPCPlacementManager() = default;
    RemoteNPCPlacementManager(const RemoteNPCPlacementManager&) = delete;
    RemoteNPCPlacementManager& operator=(const RemoteNPCPlacementManager&) = delete;

    bool m_isActive = false;
    RE::Actor* m_npc = nullptr;
    RE::NiPoint3 m_initialPosition;  // Original position (for potential future use)
};

} // namespace Grab
