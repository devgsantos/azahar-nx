// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_paths.h"

#include <array>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/switch_trace.h"
#include "switch_debug_log.h"

namespace Azahar::Switch {
namespace {

constexpr std::array RequiredDirs{
    "sdmc:/switch/azahar/config/",
    "sdmc:/switch/azahar/cache/",
    "sdmc:/switch/azahar/logs/",
    "sdmc:/switch/azahar/resources/",
    "sdmc:/switch/azahar/roms/",
    "sdmc:/switch/azahar/userdata/",
    "sdmc:/switch/azahar/userdata/saves/",
    "sdmc:/switch/azahar/userdata/states/",
    "sdmc:/switch/azahar/userdata/screenshots/",
    "sdmc:/switch/azahar/userdata/cheats/",
    "sdmc:/switch/azahar/userdata/shaders/",
    "sdmc:/switch/azahar/userdata/nand/",
    "sdmc:/switch/azahar/userdata/sdmc/",
    "sdmc:/switch/azahar/userdata/sysdata/",
    "sdmc:/switch/azahar/userdata/keys/",
    "sdmc:/switch/azahar/userdata/bios/",
};

void LogPathStatus(const char* label, const std::string& path, bool exists) {
    SWITCH_EARLY_LOGF("LogPathStatus enter label=%s path=%s exists=%s", label, path.c_str(),
                      exists ? "true" : "false");
    LOG_INFO(Frontend, "Switch external data {}: {} ({})", label, path,
             exists ? "found" : "missing");
    SWITCH_EARLY_LOGF("external data %s path=%s exists=%s", label, path.c_str(),
                      exists ? "true" : "false");
    SWITCH_EARLY_LOGF("LogPathStatus leave label=%s", label);
}

} // namespace

bool InitializeFilesystemLayout() {
    SWITCH_TRACE_SCOPE("Switch.Paths", "InitializeFilesystemLayout");
    bool ok = true;
    for (const char* dir : RequiredDirs) {
        const bool created = FileUtil::CreateFullPath(dir);
        SWITCH_EARLY_LOGF("CreateFullPath %s result=%s", dir, created ? "true" : "false");
        ok = created && ok;
    }

    FileUtil::SetUserPath("sdmc:/switch/azahar/userdata/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::ConfigDir, "sdmc:/switch/azahar/config/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::CacheDir, "sdmc:/switch/azahar/cache/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::LogDir, "sdmc:/switch/azahar/logs/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::SDMCDir, "sdmc:/switch/azahar/userdata/sdmc/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::NANDDir, "sdmc:/switch/azahar/userdata/nand/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::SysDataDir,
                             "sdmc:/switch/azahar/userdata/sysdata/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::CheatsDir,
                             "sdmc:/switch/azahar/userdata/cheats/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::ShaderDir,
                             "sdmc:/switch/azahar/userdata/shaders/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::StatesDir,
                             "sdmc:/switch/azahar/userdata/states/");
    FileUtil::UpdateUserPath(FileUtil::UserPath::DumpDir,
                             "sdmc:/switch/azahar/userdata/screenshots/");

    return ok;
}

void ApplySwitchSettings() {
    SWITCH_TRACE_SCOPE("Switch.Paths", "ApplySwitchSettings");
    Settings::values.use_cpu_jit = true;
    Settings::values.cpu_clock_percentage = 100;
    Settings::values.graphics_api = Settings::GraphicsAPI::Deko3D;
    Settings::values.resolution_factor = 1;
    Settings::values.use_disk_shader_cache = true;
    Settings::values.async_shader_compilation = true;
    Settings::values.texture_filter = Settings::TextureFilter::NoFilter;
    Settings::values.texture_sampling = Settings::TextureSampling::GameControlled;
    Settings::values.custom_textures = false;
    Settings::values.preload_textures = false;
    Settings::values.filter_mode = false;
    Settings::values.pp_shader_name = "None (builtin)";
    Settings::values.audio_emulation = Settings::AudioEmulation::HLE;
    Settings::values.output_type = AudioCore::SinkType::Null;
    Settings::values.input_type = AudioCore::InputType::Null;
    Settings::values.frame_limit = 100.0;
    Settings::values.use_gdbstub = false;
    Settings::values.enable_rpc_server = false;

    LOG_INFO(Frontend, "Switch performance profile applied: JIT forced on, native resolution, "
                       "shader cache on, texture cache on, expensive enhancements off");
    LOG_INFO(Render, "Renderer backend selected: Deko3D");
}

bool IsSwitchRendererBackendAvailable() {
    return Settings::GetWorkingGraphicsAPI() == Settings::GraphicsAPI::Deko3D;
}

ExternalDataStatus CheckExternalData() {
    SWITCH_TRACE_SCOPE("Switch.Paths", "CheckExternalData");
    SWITCH_EARLY_LOG("CheckExternalData entered");
    ExternalDataStatus status;

    const std::string aes_keys = std::string(KeysPath) + "aes_keys.txt";
    const std::string seeddb = std::string(SysDataPath) + "seeddb.bin";
    const std::string shared_font = std::string(SysDataPath) + "shared_font.bin";

    SWITCH_EARLY_LOGF("CheckExternalData Exists enter path=%s", aes_keys.c_str());
    status.aes_keys = FileUtil::Exists(aes_keys);
    SWITCH_EARLY_LOGF("CheckExternalData Exists leave path=%s exists=%s", aes_keys.c_str(),
                      status.aes_keys ? "true" : "false");
    SWITCH_EARLY_LOGF("CheckExternalData Exists enter path=%s", seeddb.c_str());
    status.seeddb = FileUtil::Exists(seeddb);
    SWITCH_EARLY_LOGF("CheckExternalData Exists leave path=%s exists=%s", seeddb.c_str(),
                      status.seeddb ? "true" : "false");
    SWITCH_EARLY_LOGF("CheckExternalData Exists enter path=%s", shared_font.c_str());
    status.shared_font = FileUtil::Exists(shared_font);
    SWITCH_EARLY_LOGF("CheckExternalData Exists leave path=%s exists=%s", shared_font.c_str(),
                      status.shared_font ? "true" : "false");
    SWITCH_EARLY_LOGF("CheckExternalData IsDirectory enter path=%s", NandPath);
    status.nand_dir = FileUtil::IsDirectory(NandPath);
    SWITCH_EARLY_LOGF("CheckExternalData IsDirectory leave path=%s exists=%s", NandPath,
                      status.nand_dir ? "true" : "false");
    SWITCH_EARLY_LOGF("CheckExternalData IsDirectory enter path=%s", SdmcPath);
    status.sdmc_dir = FileUtil::IsDirectory(SdmcPath);
    SWITCH_EARLY_LOGF("CheckExternalData IsDirectory leave path=%s exists=%s", SdmcPath,
                      status.sdmc_dir ? "true" : "false");

    LogPathStatus("aes_keys.txt", aes_keys, status.aes_keys);
    LogPathStatus("seeddb.bin", seeddb, status.seeddb);
    LogPathStatus("shared_font.bin", shared_font, status.shared_font);
    LogPathStatus("NAND directory", NandPath, status.nand_dir);
    LogPathStatus("SDMC directory", SdmcPath, status.sdmc_dir);

    if (!status.aes_keys) {
        status.warnings.emplace_back("Missing userdata/keys/aes_keys.txt");
    }
    if (!status.seeddb) {
        status.warnings.emplace_back("Missing userdata/sysdata/seeddb.bin");
    }
    if (!status.shared_font) {
        status.warnings.emplace_back("Missing userdata/sysdata/shared_font.bin");
    }
    if (!status.nand_dir) {
        status.warnings.emplace_back("Missing userdata/nand/");
    }
    if (!status.sdmc_dir) {
        status.warnings.emplace_back("Missing userdata/sdmc/");
    }

    SWITCH_EARLY_LOG("CheckExternalData leaving");
    return status;
}

std::string LogPath() {
    // Common::Log::Initialize prepends FileUtil::UserPath::LogDir internally.
    // Return only the file name; returning a full sdmc path creates an invalid nested path.
    return "azahar.log";
}

} // namespace Azahar::Switch
