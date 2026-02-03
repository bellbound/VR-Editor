#pragma once

#include "../IFrameUpdateListener.h"
#include <RE/Skyrim.h>
#include <vector>
#include <cstdint>

namespace Grab {

// DeferredCollisionUpdateManager: Prevents players from falling through objects
// when collision is updated/reset during placement finalization.
//
// Problem: When we call Disable()/Enable() on an object to reset its collision,
// if the player is standing on that object, they will fall through momentarily
// because the collision briefly disappears.
//
// Solution: If the player is standing on the object when placement is finalized:
// 1. Register the object with this manager instead of updating collision immediately
// 2. Check every 90 frames (~1.5 sec at 60fps) if player stopped colliding
// 3. Once player leaves, wait 1 second and verify they haven't returned
// 4. If clear, trigger the Disable/Enable toggle to finalize collision
//
// Detection: Uses bhkCharacterController::supportBody to check what rigid body
// the player is currently standing on, comparing it to the object's collision body.

class DeferredCollisionUpdateManager : public IFrameUpdateListener
{
public:
    static DeferredCollisionUpdateManager* GetSingleton();

    void Initialize();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    // IFrameUpdateListener interface
    void OnFrameUpdate(float deltaTime) override;

    // Register an object for deferred collision update.
    // Returns true if object was registered (player is standing on it).
    // Returns false if player is not on the object (caller should update immediately).
    bool RegisterForDeferredUpdate(RE::TESObjectREFR* ref, const RE::NiTransform& finalTransform);

    // Check if an object is currently pending deferred update
    bool IsPendingUpdate(RE::TESObjectREFR* ref) const;

    // Force immediate update for an object (e.g., on shutdown or cell unload)
    void ForceImmediateUpdate(RE::TESObjectREFR* ref);

    // Clear all pending updates (e.g., on edit mode exit)
    void ClearAll();

    // Check if player is currently standing on the given object
    // Uses bhkCharacterController::supportBody to compare against the object's rigid body
    bool IsPlayerStandingOn(RE::TESObjectREFR* ref) const;

    // Check if any NPC is currently standing on the given object
    // Iterates through nearby actors and checks their character controllers
    bool IsNPCStandingOn(RE::TESObjectREFR* ref) const;

    // Check if any actor (player or NPC) is standing on the given object
    // Returns true if either player or any NPC is standing on it
    bool IsAnyActorStandingOn(RE::TESObjectREFR* ref) const;

    // Check if a specific actor is standing on the given rigid body
    bool IsActorStandingOn(RE::Actor* actor, RE::hkpRigidBody* objectRigidBody) const;

private:
    DeferredCollisionUpdateManager() = default;
    ~DeferredCollisionUpdateManager() = default;
    DeferredCollisionUpdateManager(const DeferredCollisionUpdateManager&) = delete;
    DeferredCollisionUpdateManager& operator=(const DeferredCollisionUpdateManager&) = delete;

    // State machine for each pending object
    enum class PendingState {
        WaitingForPlayerToLeave,  // Initial state: checking if player left
        WaitingCooldown,          // Player left, waiting 1 second before finalizing
    };

    struct PendingObject {
        RE::TESObjectREFR* ref = nullptr;
        RE::FormID formId = 0;
        RE::NiTransform finalTransform;
        PendingState state = PendingState::WaitingForPlayerToLeave;
        std::uint32_t frameCounter = 0;  // Counts frames for check interval
        float cooldownTimer = 0.0f;      // Seconds remaining in cooldown
    };

    // Get the player's character controller
    RE::bhkCharacterController* GetPlayerCharController() const;

    // Perform the actual Disable/Enable toggle
    void PerformCollisionUpdate(RE::TESObjectREFR* ref, const RE::NiTransform& transform);

    // Configuration
    static constexpr std::uint32_t kCheckIntervalFrames = 90;  // Check every 90 frames
    static constexpr float kCooldownSeconds = 1.0f;            // Wait 1 second after player leaves

    std::vector<PendingObject> m_pendingObjects;
    bool m_initialized = false;
    bool m_isRegistered = false;
};

} // namespace Grab
