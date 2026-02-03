#include "SnapToGridController.h"
#include "../config/ConfigOptions.h"
#include "../config/ConfigStorage.h"
#include "../util/PositioningUtil.h"
#include "../util/RotationMath.h"
#include <algorithm>
#include <cmath>

namespace Grab {

// Bring rotation math utilities into scope
using Util::RotationMath::ExtractZRotation;

namespace {

float ClampPositionGridSize(float size)
{
    return std::clamp(size, 5.0f, 128.0f);
}

int ClampRotationSnapStops(int stops)
{
    return std::clamp(stops, 1, 360);
}

float GetPositionGridSizeFromConfig()
{
    auto* config = Config::ConfigStorage::GetSingleton();
    if (!config || !config->IsInitialized()) {
        return SnapToGridController::kDefaultPositionGridSize;
    }

    float size = config->GetFloat(Config::Options::kPositionGridSize,
        SnapToGridController::kDefaultPositionGridSize);
    return ClampPositionGridSize(size);
}

bool GetRotationSnappingEnabledFromConfig()
{
    auto* config = Config::ConfigStorage::GetSingleton();
    if (!config || !config->IsInitialized()) {
        return true;
    }

    return config->GetInt(Config::Options::kRotationSnappingEnabled, 1) != 0;
}

int GetRotationSnapStopsFromConfig()
{
    auto* config = Config::ConfigStorage::GetSingleton();
    if (!config || !config->IsInitialized()) {
        return SnapToGridController::kDefaultRotationSnapStops;
    }

    int stops = config->GetInt(Config::Options::kRotationSnappingStops,
        SnapToGridController::kDefaultRotationSnapStops);
    return ClampRotationSnapStops(stops);
}

float GetRotationGridDegreesFromConfig()
{
    int stops = GetRotationSnapStopsFromConfig();
    if (stops <= 0) {
        stops = SnapToGridController::kDefaultRotationSnapStops;
    }
    return 360.0f / static_cast<float>(stops);
}

} // namespace

void SnapToGridController::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool SnapToGridController::IsEnabled() const
{
    return m_enabled;
}

bool SnapToGridController::IsRotationSnappingEnabled() const
{
    return GetRotationSnappingEnabledFromConfig();
}

void SnapToGridController::SetPositionGridSize(float size)
{
    float clamped = ClampPositionGridSize(size);
    auto* config = Config::ConfigStorage::GetSingleton();
    if (config && config->IsInitialized()) {
        config->SetFloat(Config::Options::kPositionGridSize, clamped);
    }
    m_positionGridSize = clamped;
}

void SnapToGridController::SetRotationGridDegrees(float degrees)
{
    if (degrees <= 0.0f) {
        return;
    }

    int stops = static_cast<int>(std::round(360.0f / degrees));
    stops = ClampRotationSnapStops(stops);
    auto* config = Config::ConfigStorage::GetSingleton();
    if (config && config->IsInitialized()) {
        config->SetInt(Config::Options::kRotationSnappingStops, stops);
    }
    m_rotationGridDegrees = 360.0f / static_cast<float>(stops);
}

float SnapToGridController::GetPositionGridSize() const
{
    return GetPositionGridSizeFromConfig();
}

float SnapToGridController::GetRotationGridDegrees() const
{
    return GetRotationGridDegreesFromConfig();
}

float SnapToGridController::GetEffectivePositionGridSize() const
{
    float gridSize = GetPositionGridSizeFromConfig();
    if (m_gridOverride.active) {
        return gridSize * m_gridOverride.scale;
    }
    return gridSize;
}

float SnapToGridController::GetEffectiveRotationOffset() const
{
    if (m_gridOverride.active) {
        return m_gridOverride.rotationOffset;
    }
    return 0.0f;
}

void SnapToGridController::Reset()
{
    m_smootherInitialized = false;
}

bool SnapToGridController::HasGridOverride() const
{
    return m_gridOverride.active;
}

const GridOverride& SnapToGridController::GetGridOverride() const
{
    return m_gridOverride;
}

void SnapToGridController::SetGridOverride(const GridOverride& override)
{
    m_gridOverride = override;
    m_gridOverride.active = true;
    // Reset smoother to immediately apply new grid alignment
    m_smootherInitialized = false;
}

void SnapToGridController::ClearGridOverride()
{
    m_gridOverride = GridOverride{};  // Reset to default (active = false)
    // Reset smoother to apply default grid
    m_smootherInitialized = false;
}

GridOverride SnapToGridController::ComputeGridOverride(
    const RE::NiPoint3& posA,
    const RE::NiPoint3& posB,
    float rotationA)
{
    GridOverride result;

    float positionGridSize = GetPositionGridSizeFromConfig();
    float rotationGridDegrees = GetRotationGridDegreesFromConfig();

    // Calculate 2D distance (horizontal plane is most common for alignment)
    float dx = posB.x - posA.x;
    float dy = posB.y - posA.y;
    float dz = posB.z - posA.z;
    float horizontalDist = std::sqrt(dx * dx + dy * dy);
    float distance3D = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Use 3D distance for grid scaling
    float distance = distance3D;
    if (distance < 0.01f) {
        // Objects are at same position, use default grid
        result.offset = posA;
        result.scale = 1.0f;
        result.rotationOffset = 0.0f;
        result.active = true;
        return result;
    }

    // Find the number of grid steps that best fits the distance
    // We want n such that (distance / n) is as close to configured grid size as possible
    int n = static_cast<int>(std::round(distance / positionGridSize));
    if (n < 1) n = 1;

    // Calculate the effective grid size
    float effectiveGridSize = distance / static_cast<float>(n);

    // Scale is ratio of effective grid size to default
    result.scale = effectiveGridSize / positionGridSize;

    // Use posA as the grid origin offset
    // This ensures posA lies exactly on a grid point
    result.offset = posA;

    // For rotation: offset the rotation grid so one step aligns with rotationA
    // Calculate the remainder when rotationA is divided by rotation grid step
    float rotationGridRad = rotationGridDegrees * (3.14159265f / 180.0f);
    result.rotationOffset = std::fmod(rotationA, rotationGridRad);
    // Normalize to [-half_step, +half_step] for consistent snapping
    if (result.rotationOffset > rotationGridRad / 2.0f) {
        result.rotationOffset -= rotationGridRad;
    } else if (result.rotationOffset < -rotationGridRad / 2.0f) {
        result.rotationOffset += rotationGridRad;
    }

    result.active = true;
    return result;
}

SnapToGridController::SnapResult SnapToGridController::ComputeSmoothedSnap(
    const RE::NiPoint3& rawPosition,
    float rawAngle,
    float deltaTime)
{
    SnapResult result;

    if (!m_enabled) {
        // Snap disabled - use raw values, reset smoother state
        result.position = rawPosition;
        result.zAngle = rawAngle;
        m_smootherInitialized = false;
        return result;
    }

    // Calculate snap target based on whether grid override is active
    RE::NiPoint3 snapTargetCenter;

    float positionGridSize = GetPositionGridSizeFromConfig();

    if (m_gridOverride.active) {
        // Grid override: snap relative to offset with scaled grid size
        float effectiveGridSize = positionGridSize * m_gridOverride.scale;

        // Snap position relative to offset
        RE::NiPoint3 relativePos = {
            rawPosition.x - m_gridOverride.offset.x,
            rawPosition.y - m_gridOverride.offset.y,
            rawPosition.z - m_gridOverride.offset.z
        };

        RE::NiPoint3 snappedRelative = SnapPositionToGrid(relativePos, effectiveGridSize);

        snapTargetCenter = {
            snappedRelative.x + m_gridOverride.offset.x,
            snappedRelative.y + m_gridOverride.offset.y,
            snappedRelative.z + m_gridOverride.offset.z
        };
    } else {
        // Default: snap to world-aligned grid cells
        snapTargetCenter = SnapPositionToGrid(rawPosition, positionGridSize);
    }

    // Initialize snap smoother if this is the first frame with snap enabled
    if (!m_smootherInitialized) {
        RE::NiTransform initialSnap;
        initialSnap.translate = snapTargetCenter;
        // Use raw angle for smoother - rotation grid snapping happens per-object
        initialSnap.rotate = PositioningUtil::RotationAroundZ(rawAngle);
        initialSnap.scale = 1.0f;
        m_smoother.SetCurrent(initialSnap);
        m_smootherInitialized = true;
    }

    // Set target and update snap smoother
    RE::NiTransform snapTarget;
    snapTarget.translate = snapTargetCenter;
    // Raw angle passed to smoother - final world rotation snapping is per-object
    snapTarget.rotate = PositioningUtil::RotationAroundZ(rawAngle);
    snapTarget.scale = 1.0f;

    m_smoother.SetSpeed(kDefaultSmoothingSpeed);
    m_smoother.SetTarget(snapTarget);
    m_smoother.Update(deltaTime);

    // Use smoothed snap values
    RE::NiTransform smoothedSnap = m_smoother.GetCurrent();
    result.position = smoothedSnap.translate;
    result.zAngle = ExtractZRotation(smoothedSnap.rotate);

    return result;
}

RE::NiPoint3 SnapToGridController::SnapPositionToGrid(const RE::NiPoint3& pos, float gridSize)
{
    return RE::NiPoint3{
        std::round(pos.x / gridSize) * gridSize,
        std::round(pos.y / gridSize) * gridSize,
        std::round(pos.z / gridSize) * gridSize
    };
}

float SnapToGridController::SnapAngleToGrid(float angleRad, float gridDegrees)
{
    float gridRad = gridDegrees * (3.14159265f / 180.0f);
    return std::round(angleRad / gridRad) * gridRad;
}

} // namespace Grab
