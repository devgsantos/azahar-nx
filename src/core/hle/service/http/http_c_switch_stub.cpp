// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/service/http/http_c.h"

SERIALIZE_EXPORT_IMPL(Service::HTTP::HTTP_C)
SERIALIZE_EXPORT_IMPL(Service::HTTP::SessionData)

namespace Service::HTTP {

HTTP_C::HTTP_C() : ServiceFramework("http:C", 32) {}

std::shared_ptr<HTTP_C> GetService(Core::System& system) {
    return system.ServiceManager().GetService<HTTP_C>("http:C");
}

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    std::make_shared<HTTP_C>()->InstallAsService(service_manager);
}

} // namespace Service::HTTP
