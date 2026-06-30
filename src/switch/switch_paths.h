// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <string>
#include <vector>

namespace Azahar::Switch {

constexpr const char* RootPath = "sdmc:/switch/azahar/";
constexpr const char* RomPath = "sdmc:/switch/azahar/roms/";
constexpr const char* UserDataPath = "sdmc:/switch/azahar/userdata/";
constexpr const char* KeysPath = "sdmc:/switch/azahar/userdata/keys/";
constexpr const char* SysDataPath = "sdmc:/switch/azahar/userdata/sysdata/";
constexpr const char* NandPath = "sdmc:/switch/azahar/userdata/nand/";
constexpr const char* SdmcPath = "sdmc:/switch/azahar/userdata/sdmc/";

struct ExternalDataStatus {
    bool aes_keys = false;
    bool seeddb = false;
    bool shared_font = false;
    bool nand_dir = false;
    bool sdmc_dir = false;
    std::vector<std::string> warnings;

    bool HasWarnings() const {
        return !warnings.empty();
    }
};

bool InitializeFilesystemLayout();
void ApplySwitchSettings();
bool IsSwitchRendererBackendAvailable();
ExternalDataStatus CheckExternalData();
std::string LogPath();

} // namespace Azahar::Switch
