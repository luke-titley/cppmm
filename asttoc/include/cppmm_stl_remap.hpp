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
    // Namespace
    std::string namespace_qualified(const std::string & val);
    std::string namespace_short(const std::string & val);

    // Record
    std::string record_qualified(const std::string & val);
    std::string record_short(const std::string & val);
} // namespace remap

} // namespace cppmm
