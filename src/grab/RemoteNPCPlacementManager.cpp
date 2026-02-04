#include "RemoteNPCPlacementManager.h"
#include "../log.h"

namespace Grab {

RemoteNPCPlacementManager* RemoteNPCPlacementManager::GetSingleton()
{
    static RemoteNPCPlacementManager instance;
    return &instance;
}

bool RemoteNPCPlacementManager::IsNPC(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return false;
    }

    // Check if this is an Actor (but not the player)
    auto* actor = ref->As<RE::Actor>();
    if (!actor) {
        return false;
    }

    // Exclude the player
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (actor == player) {
        return false;
    }

    return true;
}

bool RemoteNPCPlacementManager::OnEnter(RE::Actor* npc, float initialDistance)
{
    if (!npc) {
        spdlog::warn("RemoteNPCPlacementManager::OnEnter: null NPC");
        return false;
    }

    if (m_isActive) {
        spdlog::warn("RemoteNPCPlacementManager::OnEnter: already active");
        return false;
    }

    m_npc = npc;
    m_initialPosition = npc->GetPosition();
    m_isActive = true;

    spdlog::info("RemoteNPCPlacementManager::OnEnter: Started NPC placement for {:08X} ({})",
        npc->GetFormID(), npc->GetName());

    return true;
}

std::optional<NPCMoveResult> RemoteNPCPlacementManager::OnExit()
{
    if (!m_isActive) {
        return std::nullopt;
    }

    std::optional<NPCMoveResult> result;

    if (m_npc) {
        result = NPCMoveResult{
            m_npc->GetFormID(),
            m_initialPosition,
            m_npc->GetPosition()
        };

        spdlog::info("RemoteNPCPlacementManager::OnExit: Ended NPC placement for {:08X}", m_npc->GetFormID());
    }

    m_isActive = false;
    m_npc = nullptr;
    return result;
}

void RemoteNPCPlacementManager::UpdatePosition(const RE::NiPoint3& centerPos)
{
    if (!m_isActive || !m_npc) {
        return;
    }

    // Check if NPC is still valid
    if (!m_npc->Get3D()) {
        spdlog::warn("RemoteNPCPlacementManager: NPC 3D became invalid");
        OnExit();
        return;
    }

    // Move the NPC to the target position using Actor::SetPosition
    // Second parameter (true) updates the character controller for proper physics
    m_npc->SetPosition(centerPos, true);
}

} // namespace Grab
