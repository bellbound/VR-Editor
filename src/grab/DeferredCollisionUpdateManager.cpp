#include "DeferredCollisionUpdateManager.h"
#include "../FrameCallbackDispatcher.h"
#include "../selection/SelectionState.h"
#include "../visuals/ObjectHighlighter.h"
#include "../log.h"
#include <algorithm>

namespace Grab {

DeferredCollisionUpdateManager* DeferredCollisionUpdateManager::GetSingleton()
{
    static DeferredCollisionUpdateManager instance;
    return &instance;
}

void DeferredCollisionUpdateManager::Initialize()
{
    if (m_initialized) {
        spdlog::warn("DeferredCollisionUpdateManager already initialized");
        return;
    }

    m_initialized = true;
    spdlog::info("DeferredCollisionUpdateManager initialized");
}

void DeferredCollisionUpdateManager::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    // Force complete all pending updates
    for (auto& pending : m_pendingObjects) {
        if (pending.ref) {
            spdlog::info("DeferredCollisionUpdateManager: Forcing update on shutdown for {:08X}",
                pending.formId);
            PerformCollisionUpdate(pending.ref, pending.finalTransform);
        }
    }
    m_pendingObjects.clear();

    if (m_isRegistered) {
        FrameCallbackDispatcher::GetSingleton()->Unregister(this);
        m_isRegistered = false;
    }

    m_initialized = false;
    spdlog::info("DeferredCollisionUpdateManager shutdown");
}

void DeferredCollisionUpdateManager::OnFrameUpdate(float deltaTime)
{
    if (m_pendingObjects.empty()) {
        if (m_isRegistered) {
            FrameCallbackDispatcher::GetSingleton()->Unregister(this);
            m_isRegistered = false;
        }
        return;
    }

    // Process all pending objects
    // Use index-based loop because we may remove elements
    for (size_t i = 0; i < m_pendingObjects.size(); ) {
        auto& pending = m_pendingObjects[i];

        // Validate object still exists
        if (!pending.ref || !pending.ref->Get3D()) {
            spdlog::warn("DeferredCollisionUpdateManager: Object {:08X} became invalid, removing",
                pending.formId);
            m_pendingObjects.erase(m_pendingObjects.begin() + i);
            continue;
        }

        bool shouldRemove = false;

        switch (pending.state) {
            case PendingState::WaitingForPlayerToLeave:
                pending.frameCounter++;

                // Only check every N frames to reduce overhead
                if (pending.frameCounter >= kCheckIntervalFrames) {
                    pending.frameCounter = 0;

                    if (!IsPlayerStandingOn(pending.ref)) {
                        // Player has left! Start cooldown
                        spdlog::info("DeferredCollisionUpdateManager: Player left {:08X}, starting cooldown",
                            pending.formId);
                        pending.state = PendingState::WaitingCooldown;
                        pending.cooldownTimer = kCooldownSeconds;
                    } else {
                        spdlog::trace("DeferredCollisionUpdateManager: Player still on {:08X}, continuing to wait",
                            pending.formId);
                    }
                }
                break;

            case PendingState::WaitingCooldown:
                pending.cooldownTimer -= deltaTime;

                if (pending.cooldownTimer <= 0.0f) {
                    // Cooldown complete - verify player hasn't returned
                    if (IsPlayerStandingOn(pending.ref)) {
                        // Player came back! Go back to waiting
                        spdlog::info("DeferredCollisionUpdateManager: Player returned to {:08X}, resetting",
                            pending.formId);
                        pending.state = PendingState::WaitingForPlayerToLeave;
                        pending.frameCounter = 0;
                    } else {
                        // All clear - perform the update!
                        spdlog::info("DeferredCollisionUpdateManager: Finalizing collision update for {:08X}",
                            pending.formId);
                        PerformCollisionUpdate(pending.ref, pending.finalTransform);
                        shouldRemove = true;
                    }
                }
                break;
        }

        if (shouldRemove) {
            m_pendingObjects.erase(m_pendingObjects.begin() + i);
        } else {
            ++i;
        }
    }

    if (m_pendingObjects.empty() && m_isRegistered) {
        FrameCallbackDispatcher::GetSingleton()->Unregister(this);
        m_isRegistered = false;
    }
}

bool DeferredCollisionUpdateManager::RegisterForDeferredUpdate(RE::TESObjectREFR* ref,
                                                                const RE::NiTransform& finalTransform)
{
    if (!ref) {
        return false;
    }

    // Check if player is standing on this object
    if (!IsPlayerStandingOn(ref)) {
        // Player is not on this object - caller should update immediately
        return false;
    }

    // Check if already registered
    auto it = std::find_if(m_pendingObjects.begin(), m_pendingObjects.end(),
        [ref](const PendingObject& obj) { return obj.ref == ref; });

    if (it != m_pendingObjects.end()) {
        // Already registered - update the transform
        spdlog::info("DeferredCollisionUpdateManager: Updating existing entry for {:08X}",
            ref->GetFormID());
        it->finalTransform = finalTransform;
        // Reset state to restart the wait
        it->state = PendingState::WaitingForPlayerToLeave;
        it->frameCounter = 0;
        return true;
    }

    // Add new pending object
    bool wasEmpty = m_pendingObjects.empty();
    PendingObject pending;
    pending.ref = ref;
    pending.formId = ref->GetFormID();
    pending.finalTransform = finalTransform;
    pending.state = PendingState::WaitingForPlayerToLeave;
    pending.frameCounter = 0;
    pending.cooldownTimer = 0.0f;

    m_pendingObjects.push_back(pending);

    if (wasEmpty && !m_isRegistered) {
        FrameCallbackDispatcher::GetSingleton()->Register(this, false);
        m_isRegistered = true;
    }

    spdlog::info("DeferredCollisionUpdateManager: Registered {:08X} for deferred update (player is standing on it)",
        pending.formId);

    return true;
}

bool DeferredCollisionUpdateManager::IsPendingUpdate(RE::TESObjectREFR* ref) const
{
    if (!ref) return false;

    auto it = std::find_if(m_pendingObjects.begin(), m_pendingObjects.end(),
        [ref](const PendingObject& obj) { return obj.ref == ref; });

    return it != m_pendingObjects.end();
}

void DeferredCollisionUpdateManager::ForceImmediateUpdate(RE::TESObjectREFR* ref)
{
    if (!ref) return;

    auto it = std::find_if(m_pendingObjects.begin(), m_pendingObjects.end(),
        [ref](const PendingObject& obj) { return obj.ref == ref; });

    if (it != m_pendingObjects.end()) {
        spdlog::info("DeferredCollisionUpdateManager: Forcing immediate update for {:08X}",
            it->formId);
        PerformCollisionUpdate(it->ref, it->finalTransform);
        m_pendingObjects.erase(it);
        if (m_pendingObjects.empty() && m_isRegistered) {
            FrameCallbackDispatcher::GetSingleton()->Unregister(this);
            m_isRegistered = false;
        }
    }
}

void DeferredCollisionUpdateManager::ClearAll()
{
    // Force all pending updates immediately
    for (auto& pending : m_pendingObjects) {
        if (pending.ref) {
            spdlog::info("DeferredCollisionUpdateManager: ClearAll - forcing update for {:08X}",
                pending.formId);
            PerformCollisionUpdate(pending.ref, pending.finalTransform);
        }
    }
    m_pendingObjects.clear();
    if (m_isRegistered) {
        FrameCallbackDispatcher::GetSingleton()->Unregister(this);
        m_isRegistered = false;
    }
}

bool DeferredCollisionUpdateManager::IsPlayerStandingOn(RE::TESObjectREFR* ref) const
{
    if (!ref) return false;

    // Get the object's 3D node and its collision object
    auto* node = ref->Get3D();
    if (!node) return false;

    auto* collisionObject = node->GetCollisionObject();
    if (!collisionObject) return false;

    // Get the rigid body wrapper
    auto* bhkRigid = collisionObject->GetRigidBody();
    if (!bhkRigid) return false;

    // Get the underlying Havok rigid body (the actual hkpRigidBody*)
    auto* objectRigidBody = bhkRigid->referencedObject.get();
    if (!objectRigidBody) return false;

    // Get the player's character controller
    auto* charController = GetPlayerCharController();
    if (!charController) return false;

    // Check if the supportBody matches our object's rigid body
    // supportBody is what the player is currently standing on
    auto* supportBody = charController->supportBody.get();

    if (supportBody == objectRigidBody) {
        spdlog::trace("DeferredCollisionUpdateManager: Player supportBody matches {:08X}",
            ref->GetFormID());
        return true;
    }

    return false;
}

bool DeferredCollisionUpdateManager::IsActorStandingOn(RE::Actor* actor, RE::hkpRigidBody* objectRigidBody) const
{
    if (!actor || !objectRigidBody) return false;

    // Get the actor's character controller
    auto* charController = actor->GetCharController();
    if (!charController) return false;

    // Check if the supportBody matches our object's rigid body
    auto* supportBody = charController->supportBody.get();

    return supportBody == objectRigidBody;
}

bool DeferredCollisionUpdateManager::IsNPCStandingOn(RE::TESObjectREFR* ref) const
{
    if (!ref) return false;

    // Get the object's 3D node and its collision object
    auto* node = ref->Get3D();
    if (!node) return false;

    auto* collisionObject = node->GetCollisionObject();
    if (!collisionObject) return false;

    // Get the rigid body wrapper
    auto* bhkRigid = collisionObject->GetRigidBody();
    if (!bhkRigid) return false;

    // Get the underlying Havok rigid body
    auto* objectRigidBody = bhkRigid->GetRigidBody();
    if (!objectRigidBody) return false;

    // Get the cell containing the object
    auto* cell = ref->GetParentCell();
    if (!cell) return false;

    // Search for NPCs near the object
    // Use a reasonable search radius (500 units should cover most cases)
    RE::NiPoint3 objPos = ref->GetPosition();
    constexpr float kSearchRadius = 500.0f;

    bool foundNPC = false;

    cell->ForEachReferenceInRange(objPos, kSearchRadius,
        [&](RE::TESObjectREFR* candidate) -> RE::BSContainer::ForEachResult {
            // Skip if null or is the object itself
            if (!candidate || candidate == ref) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check if it's an actor (NPC)
            auto* actor = candidate->As<RE::Actor>();
            if (!actor) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Skip the player - we have a separate check for that
            if (actor->IsPlayerRef()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Skip dead NPCs
            if (actor->IsDead()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check if this NPC is standing on our object
            if (IsActorStandingOn(actor, objectRigidBody)) {
                spdlog::trace("DeferredCollisionUpdateManager: NPC {:08X} is standing on {:08X}",
                    actor->GetFormID(), ref->GetFormID());
                foundNPC = true;
                return RE::BSContainer::ForEachResult::kStop;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

    return foundNPC;
}

bool DeferredCollisionUpdateManager::IsAnyActorStandingOn(RE::TESObjectREFR* ref) const
{
    if (!ref) return false;

    // Check player first (most common case, and cheaper check)
    if (IsPlayerStandingOn(ref)) {
        return true;
    }

    // Check NPCs
    if (IsNPCStandingOn(ref)) {
        return true;
    }

    return false;
}

RE::bhkCharacterController* DeferredCollisionUpdateManager::GetPlayerCharController() const
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return nullptr;

    return player->GetCharController();
}

void DeferredCollisionUpdateManager::PerformCollisionUpdate(RE::TESObjectREFR* ref,
                                                             const RE::NiTransform& transform)
{
    if (!ref) return;

    // Disable/Enable cycle forces Havok to rebuild collision at new position
    // This is the nuclear option that ensures collision boxes are updated
    ref->Disable();
    ref->Enable(false);  // false = don't reset inventory

    // Refresh highlight - ObjectHighlighter will automatically defer if 3D isn't ready yet
    Selection::SelectionState::GetSingleton()->RefreshHighlightIfSelected(ref);

    spdlog::info("DeferredCollisionUpdateManager: Performed Disable/Enable on {:08X}",
        ref->GetFormID());
}

} // namespace Grab
