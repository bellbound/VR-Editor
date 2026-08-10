#include "FormKeyUtil.h"
#include "../log.h"
#include <RE/T/TESFile.h>
#include <RE/T/TESDataHandler.h>
#include <fmt/format.h>
#include <charconv>

namespace Persistence {

std::string FormKeyUtil::BuildFormKey(RE::TESObjectREFR* ref)
{
    if (!ref) {
        return "";
    }
    return BuildFormKey(static_cast<RE::TESForm*>(ref));
}

std::string FormKeyUtil::BuildFormKey(RE::TESForm* form)
{
    if (!form) {
        return "";
    }

    // Derive the owning plugin from the runtime FormID's own load order index,
    // NOT from GetFile(0).
    //
    // GetFile(0) returns a file from the form's source-file list, which for an
    // OVERRIDDEN record can be the overriding plugin rather than the plugin that
    // actually owns the FormID. Example seen in the wild: Jorrvaskr's ember refs
    // 0x00078039/A/B are owned by Skyrim.esm but overridden by Embers XD.esp -
    // GetFile(0) reported "Embers XD.esp", so the key was written as
    // "0x78039~Embers XD.esp". ResolveToRuntimeFormID then recombined that local
    // ID with Embers XD's compile index (0x4E) producing 0x4E078039, a form that
    // does not exist, so the edit silently failed to reapply on load.
    //
    // The FormID's index byte(s) are authoritative and match what the in-game
    // console reports. Deriving from them here makes BuildFormKey the exact
    // inverse of ResolveToRuntimeFormID, guaranteeing a lossless round trip.
    RE::FormID formId = form->GetFormID();

    // Dynamic/runtime-created forms (0xFF...) have no source plugin at all.
    if ((formId & 0xFF000000) == 0xFF000000) {
        spdlog::trace("FormKeyUtil: Form {:08X} is dynamic (no source plugin)", formId);
        return "";
    }

    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        spdlog::error("FormKeyUtil: TESDataHandler not available");
        return "";
    }

    const RE::TESFile* file = nullptr;
    RE::FormID localId = 0;

    if ((formId & 0xFF000000) == 0xFE000000) {
        // Light plugin: FE in top byte, smallFileCompileIndex in the next 12 bits
        auto smallIndex = static_cast<std::uint16_t>((formId >> 12) & 0xFFF);
        localId = formId & 0xFFF;
        file = dataHandler->LookupLoadedLightModByIndex(smallIndex);
    } else {
        // Regular plugin: compile index in the top byte
        auto index = static_cast<std::uint8_t>(formId >> 24);
        localId = formId & 0x00FFFFFF;
        file = dataHandler->LookupLoadedModByIndex(index);
    }

    if (!file) {
        spdlog::warn("FormKeyUtil: Could not resolve owning plugin for form {:08X}", formId);
        return "";
    }

    std::string_view pluginName = file->GetFilename();

    return BuildFormKey(localId, pluginName);
}

std::string FormKeyUtil::BuildFormKey(RE::FormID localFormId, std::string_view pluginName)
{
    // Format: "0x[hex]~[name]"
    return fmt::format("0x{:X}~{}", localFormId, pluginName);
}

std::optional<FormKeyUtil::ParsedKey> FormKeyUtil::ParseFormKey(std::string_view keyString)
{
    // Expected format: "0x[hex]~[pluginName]"
    // Example: "0x10C0E3~Skyrim.esm"

    if (keyString.size() < 4) {  // Minimum: "0x0~X"
        return std::nullopt;
    }

    // Must start with "0x"
    if (keyString.substr(0, 2) != "0x") {
        return std::nullopt;
    }

    // Find the tilde separator
    size_t tildePos = keyString.find('~');
    if (tildePos == std::string_view::npos || tildePos <= 2) {
        return std::nullopt;
    }

    // Extract hex portion (after "0x", before "~")
    std::string_view hexPart = keyString.substr(2, tildePos - 2);
    if (hexPart.empty()) {
        return std::nullopt;
    }

    // Parse hex to FormID
    RE::FormID localFormId = 0;
    auto result = std::from_chars(hexPart.data(), hexPart.data() + hexPart.size(),
                                   localFormId, 16);
    if (result.ec != std::errc{} || result.ptr != hexPart.data() + hexPart.size()) {
        return std::nullopt;
    }

    // Extract plugin name (after "~")
    std::string_view pluginName = keyString.substr(tildePos + 1);
    if (pluginName.empty()) {
        return std::nullopt;
    }

    return ParsedKey{ localFormId, std::string(pluginName) };
}

RE::FormID FormKeyUtil::ResolveToRuntimeFormID(std::string_view keyString)
{
    auto parsed = ParseFormKey(keyString);
    if (!parsed) {
        spdlog::warn("FormKeyUtil: Failed to parse key string: {}", keyString);
        return 0;
    }

    // Special case: DYNAMIC forms are runtime-created objects
    // Their FormID in the key IS already the runtime FormID (0xFF range)
    if (parsed->pluginName == "DYNAMIC") {
        spdlog::trace("FormKeyUtil: Resolved DYNAMIC form key {} to {:08X}",
            keyString, parsed->localFormId);
        return parsed->localFormId;
    }

    // Find the plugin file by name
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        spdlog::error("FormKeyUtil: TESDataHandler not available");
        return 0;
    }

    const RE::TESFile* file = nullptr;

    // Search in loaded mods
    for (auto* mod : dataHandler->files) {
        if (mod && mod->GetFilename() == parsed->pluginName) {
            file = mod;
            break;
        }
    }

    if (!file) {
        spdlog::warn("FormKeyUtil: Plugin not loaded: {}", parsed->pluginName);
        return 0;
    }

    // Combine local FormID with the file's compile index to get runtime FormID
    // For regular plugins: compileIndex goes in upper byte
    // For light plugins (ESL): uses smallFileCompileIndex in different position
    RE::FormID runtimeFormId = parsed->localFormId;

    if (file->IsLight()) {
        // Light plugin: FE in top byte, smallFileCompileIndex in next 12 bits
        runtimeFormId |= (0xFE000000 | (static_cast<uint32_t>(file->GetSmallFileCompileIndex()) << 12));
    } else {
        // Regular plugin: compileIndex in top byte
        runtimeFormId |= (static_cast<uint32_t>(file->GetCompileIndex()) << 24);
    }

    return runtimeFormId;
}

} // namespace Persistence
