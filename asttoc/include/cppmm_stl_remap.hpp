#pragma once

#include <string>

namespace cppmm {
enum class StandardTemplateLib : uint32_t {
    kstdcpp = 0,
    kcpp = 1,
    // TODO LT : intel
    // TODO LT : vsc++
};

namespace remap {

// Remapped
struct Remapped {
    std::string qualified;
    std::string short_;
};

// Namespace
Remapped namespace_(StandardTemplateLib stl, const std::string& qualified,
                    const std::string& short_);

// Record
Remapped record(StandardTemplateLib stl, const std::string& qualified,
                const std::string& short_);

} // namespace remap

} // namespace cppmm
