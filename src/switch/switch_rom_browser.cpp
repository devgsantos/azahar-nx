// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_rom_browser.h"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "switch_debug_log.h"
#include "switch_input.h"
#include "switch_paths.h"

namespace Azahar::Switch {
namespace {

bool IsSupportedRom(std::string_view name) {
    const std::string ext = Common::ToLower(std::string(FileUtil::GetExtensionFromFilename(name)));
    return ext == "3ds" || ext == "cci" || ext == "cia" || ext == "3dsx";
}

std::string ExtensionOf(std::string_view name) {
    return Common::ToLower(std::string(FileUtil::GetExtensionFromFilename(name)));
}

} // namespace

void RomBrowser::Refresh() {
    SWITCH_EARLY_LOG("RomBrowser::Refresh entered");
    SWITCH_EARLY_LOGF("ROM directory path=%s", RomPath);
    entries.clear();
    FileUtil::CreateFullPath(RomPath);
    const bool scan_ok = FileUtil::ForeachDirectoryEntry(
        nullptr, RomPath,
        [this](u64*, const std::string& directory, const std::string& virtual_name) {
            const std::string path = directory + virtual_name;
            const bool is_dir = FileUtil::IsDirectory(path);
            const std::string ext = ExtensionOf(virtual_name);
            const bool supported = !is_dir && IsSupportedRom(virtual_name);
            SWITCH_EARLY_LOGF("scan entry name=%s ext=%s is_dir=%s supported=%s",
                              virtual_name.c_str(), ext.c_str(), is_dir ? "true" : "false",
                              supported ? "true" : "false");
            if (supported) {
                entries.push_back({virtual_name, path});
            }
            return true;
        });
    if (!scan_ok) {
        SWITCH_EARLY_LOG("RomBrowser::Refresh failed to open ROM directory");
        LOG_ERROR(Frontend, "Failed to scan ROM directory {}", RomPath);
    }
    std::sort(entries.begin(), entries.end(),
              [](const RomEntry& a, const RomEntry& b) { return a.name < b.name; });
    selected = std::min(selected, entries.empty() ? 0 : entries.size() - 1);
    LOG_INFO(Frontend, "ROM browser found {} supported entries", entries.size());
    SWITCH_EARLY_LOGF("ROM browser found %zu supported entries", entries.size());
}

std::optional<std::string> RomBrowser::Run() {
    SWITCH_EARLY_LOG("RomBrowser::Run opened");
    Refresh();
    InputState previous;

    while (true) {
        const InputState input = PollInput();
        const InputState pressed = NewlyPressed(previous, input);
        previous = input;

        if (pressed.down || pressed.stick_down) {
            selected = std::min(selected + 1, entries.empty() ? 0 : entries.size() - 1);
        }
        if (pressed.up || pressed.stick_up) {
            selected = selected == 0 ? 0 : selected - 1;
        }
        if (pressed.a && !entries.empty()) {
            LOG_INFO(Frontend, "Selected ROM: {}", entries[selected].path);
            SWITCH_EARLY_LOGF("Selected ROM full path=%s", entries[selected].path.c_str());
            return entries[selected].path;
        }
        if (pressed.b) {
            return std::nullopt;
        }
        if (pressed.x) {
            Refresh();
        }

        Draw();
        WaitForVBlank();
    }
}

void RomBrowser::Draw() const {
    std::printf("\x1b[2J\x1b[1;1HAzahar Switch\n\n");
    std::printf("ROM directory: %s\n", RomPath);
    std::printf("A Launch  B Exit  X Refresh  DPad/Stick Navigate\n\n");

    if (entries.empty()) {
        std::printf("No supported ROMs found.\n");
        std::printf("Copy .3ds, .cci, .cia or .3dsx files into sdmc:/switch/azahar/roms/.\n");
        return;
    }

    const std::size_t first = selected > 12 ? selected - 12 : 0;
    const std::size_t last = std::min(first + 18, entries.size());
    for (std::size_t i = first; i < last; ++i) {
        std::printf("%c %s\n", i == selected ? '>' : ' ', entries[i].name.c_str());
    }
}

} // namespace Azahar::Switch
