#include "ConsoleManager.h"
#include "../config/ConfigStorage.h"
#include "../config/ConfigOptions.h"
#include "../persistence/FormKeyUtil.h"
#include "../log.h"

#include <RE/C/Console.h>
#include <RE/U/UI.h>

namespace Selection {

void ConsoleManager::MayConsolePrid(RE::TESObjectREFR* ref)
{
    // Check if console selection is enabled
    auto* config = Config::ConfigStorage::GetSingleton();
    bool enabled = config->GetInt(Config::Options::kSelectInConsole, 1) != 0;

    if (!enabled) {
        return;
    }

    // Get console menu through UI system
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        spdlog::warn("ConsoleManager: UI singleton not available");
        return;
    }

    auto* console = ui->GetMenu<RE::Console>(RE::Console::MENU_NAME).get();
    if (!console) {
        // Console menu not currently registered - this is normal if console was never opened
        spdlog::trace("ConsoleManager: Console menu not available");
        return;
    }

    if (ref) {
        console->SetSelectedRef(ref);
        std::string formKey = Persistence::FormKeyUtil::BuildFormKey(ref);
        spdlog::info("ConsoleManager: Console prid set to {}", formKey);
    } else {
        // Clear selection by setting to null handle
        console->SetSelectedRef(static_cast<RE::TESObjectREFR*>(nullptr));
        spdlog::info("ConsoleManager: Console prid cleared");
    }
}

} // namespace Selection
