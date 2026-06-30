// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Azahar::Switch {

struct RomEntry {
    std::string name;
    std::string path;
};

class RomBrowser {
public:
    void Refresh();
    std::optional<std::string> Run();

private:
    void Draw() const;

    std::vector<RomEntry> entries;
    std::size_t selected = 0;
};

} // namespace Azahar::Switch
