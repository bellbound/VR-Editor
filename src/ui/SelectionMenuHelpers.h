#pragma once

#include <RE/Skyrim.h>
#include <string>

namespace SelectionMenuHelpers
{
    // Extract file name (without extension) from a mesh path
    // e.g., "meshes\\clutter\\vase01.nif" -> "vase01"
    inline std::wstring ExtractFileNameFromPath(const char* path)
    {
        if (!path || path[0] == '\0') return L"";

        std::string pathStr(path);

        // Find the last slash (either / or \)
        size_t lastSlash = pathStr.find_last_of("/\\");
        std::string fileName = (lastSlash != std::string::npos)
            ? pathStr.substr(lastSlash + 1)
            : pathStr;

        // Remove extension
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos != std::string::npos) {
            fileName = fileName.substr(0, dotPos);
        }

        // Convert to wide string
        if (fileName.empty()) return L"";

        std::wstring wideFileName(fileName.begin(), fileName.end());
        return wideFileName;
    }

    // Try to get display name from a TESObjectREFR's base object (via TESFullName interface)
    inline const char* GetDisplayNameFromRef(RE::TESObjectREFR* ref)
    {
        if (!ref) return nullptr;

        auto* baseObj = ref->GetBaseObject();
        if (!baseObj) return nullptr;

        // Try to cast to TESFullName interface to get the display name
        auto* fullName = baseObj->As<RE::TESFullName>();
        if (fullName) {
            const char* name = fullName->GetFullName();
            // Check for non-empty name
            if (name && name[0] != '\0') {
                return name;
            }
        }

        return nullptr;
    }

    // Try to get mesh path from a TESObjectREFR's base object (via TESModel interface)
    inline const char* GetMeshPathFromRef(RE::TESObjectREFR* ref)
    {
        if (!ref) return nullptr;

        auto* baseObj = ref->GetBaseObject();
        if (!baseObj) return nullptr;

        // Try to cast to TESModel interface to get the model path
        auto* model = baseObj->As<RE::TESModel>();
        if (model) {
            return model->GetModel();
        }

        return nullptr;
    }
}
