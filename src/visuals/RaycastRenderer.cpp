#include "RaycastRenderer.h"
#include <cmath>

namespace RaycastRenderer {

namespace {
    bool g_isVisible = false;

    // Configuration for beam stretch (matches HIGGS defaults)
    constexpr float kBeamStretchX = 0.25f;   // Width
    constexpr float kBeamStretchY = 0.022f;  // Length multiplier
    constexpr float kBeamStretchZ = 0.25f;   // Height

    // Get the SpellOrigin node from the player's VR node data
    RE::NiNode* GetSpellOriginNode() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return nullptr;
        }

        auto* vrData = player->GetVRNodeData();
        if (!vrData || !vrData->SpellOrigin) {
            return nullptr;
        }

        return vrData->SpellOrigin.get();
    }

    // Create a rotation matrix from a forward vector
    // Similar to HIGGS's MatrixFromForwardVector
    RE::NiMatrix3 MatrixFromForwardVector(const RE::NiPoint3& forward, const RE::NiPoint3& worldUp) {
        RE::NiMatrix3 result;

        // Calculate right vector (cross product of up and forward)
        RE::NiPoint3 right = {
            worldUp.y * forward.z - worldUp.z * forward.y,
            worldUp.z * forward.x - worldUp.x * forward.z,
            worldUp.x * forward.y - worldUp.y * forward.x
        };

        // Normalize right
        float rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
        if (rightLen > 0.0f) {
            right.x /= rightLen;
            right.y /= rightLen;
            right.z /= rightLen;
        }

        // Calculate actual up vector (cross product of forward and right)
        RE::NiPoint3 up = {
            forward.y * right.z - forward.z * right.y,
            forward.z * right.x - forward.x * right.z,
            forward.x * right.y - forward.y * right.x
        };

        // Set rotation matrix columns: [right, forward, up]
        result.entry[0][0] = right.x;
        result.entry[1][0] = right.y;
        result.entry[2][0] = right.z;

        result.entry[0][1] = forward.x;
        result.entry[1][1] = forward.y;
        result.entry[2][1] = forward.z;

        result.entry[0][2] = up.x;
        result.entry[1][2] = up.y;
        result.entry[2][2] = up.z;

        return result;
    }

    // Set node visibility flag (bit 0: 0=visible, 1=hidden)
    void SetNodeVisible(RE::NiAVObject* node, bool visible) {
        if (!node) return;

        if (visible) {
            node->GetFlags().reset(RE::NiAVObject::Flag::kHidden);
        } else {
            node->GetFlags().set(RE::NiAVObject::Flag::kHidden);
        }
    }

    // Update the SpellOrigin node to render a line from start to end
    void UpdateBeamTransform(RE::NiNode* spellOrigin, const LineParams& line, BeamType type) {
        if (!spellOrigin) return;

        // SpellOrigin has at least 2 children: [0] = normal arc, [1] = off-limits arc
        auto& children = spellOrigin->GetChildren();
        if (children.size() < 2) {
            return;
        }

        auto* arcNodeDefault = children[0].get();
        auto* arcNodeInvalid = children[1].get();

        if (!arcNodeDefault || !arcNodeInvalid) {
            return;
        }

        // Calculate line properties
        float distance = line.Length();
        if (distance <= 0.0f) {
            return;
        }

        RE::NiPoint3 direction = line.Direction();
        RE::NiPoint3 endToStart = {-direction.x, -direction.y, -direction.z};

        // Create stretch matrix based on distance
        RE::NiMatrix3 stretcher;
        stretcher.entry[0][0] = kBeamStretchX;
        stretcher.entry[0][1] = 0.0f;
        stretcher.entry[0][2] = 0.0f;

        stretcher.entry[1][0] = 0.0f;
        stretcher.entry[1][1] = distance * kBeamStretchY;  // Length scaled by distance
        stretcher.entry[1][2] = 0.0f;

        stretcher.entry[2][0] = 0.0f;
        stretcher.entry[2][1] = 0.0f;
        stretcher.entry[2][2] = kBeamStretchZ;

        // Create rotation matrix pointing from end toward start
        RE::NiMatrix3 rotation = MatrixFromForwardVector(endToStart, {0.0f, 0.0f, 1.0f});

        // Combine rotation and stretch: result = rotation * stretch
        RE::NiMatrix3 combined;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                combined.entry[i][j] =
                    rotation.entry[i][0] * stretcher.entry[0][j] +
                    rotation.entry[i][1] * stretcher.entry[1][j] +
                    rotation.entry[i][2] * stretcher.entry[2][j];
            }
        }

        // Position beam at the end point (destination), oriented toward start (origin)
        spellOrigin->local.translate = line.end;
        spellOrigin->local.rotate = combined;

        // Show SpellOrigin node
        SetNodeVisible(spellOrigin, true);

        // Show/hide appropriate arc based on beam type
        if (type == BeamType::Invalid) {
            SetNodeVisible(arcNodeDefault, false);
            SetNodeVisible(arcNodeInvalid, true);
        } else {
            SetNodeVisible(arcNodeDefault, true);
            SetNodeVisible(arcNodeInvalid, false);
        }

        // Update node transforms
        RE::NiUpdateData updateData;
        spellOrigin->Update(updateData);
    }

} // anonymous namespace

void Show(const LineParams& line, BeamType type) {
    auto* spellOrigin = GetSpellOriginNode();
    if (!spellOrigin) {
        return;
    }

    UpdateBeamTransform(spellOrigin, line, type);
    g_isVisible = true;
}

void Hide() {
    auto* spellOrigin = GetSpellOriginNode();
    if (spellOrigin) {
        SetNodeVisible(spellOrigin, false);
    }
    g_isVisible = false;
}

void Update(const LineParams& line, BeamType type) {
    if (!g_isVisible) {
        Show(line, type);
        return;
    }

    auto* spellOrigin = GetSpellOriginNode();
    if (!spellOrigin) {
        return;
    }

    UpdateBeamTransform(spellOrigin, line, type);
}

bool IsVisible() {
    return g_isVisible;
}

} // namespace RaycastRenderer
