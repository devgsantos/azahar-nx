// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/service/soc/soc_u.h"

SERIALIZE_EXPORT_IMPL(Service::SOC::SOC_U)

namespace Service::SOC {

SOC_U::SOC_U() : ServiceFramework("soc:U", 4) {}

SOC_U::~SOC_U() = default;

std::optional<SOC_U::InterfaceInfo> SOC_U::GetDefaultInterfaceInfo() {
    return std::nullopt;
}

std::shared_ptr<SOC_U> GetService(Core::System& system) {
    return system.ServiceManager().GetService<SOC_U>("soc:U");
}

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    std::make_shared<SOC_U>()->InstallAsService(service_manager);
}

} // namespace Service::SOC
