//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_stl_remap.hpp"
#include "pystring.h"

// _LIBCPP_VERSION = libc++
// __GLIBCXX__ = libstdc++

namespace cppmm {
namespace remap {

namespace libcpp {

//------------------------------------------------------------------------------
// libcpp/clang
//------------------------------------------------------------------------------
Remapped namespace_(const std::string& qualified, const std::string& short_) {

    if (qualified == "std::__1") {
        return Remapped{"", ""};
    }

    return Remapped{qualified, short_};
}

Remapped record(const std::string& qualified, const std::string& short_) {
    if (pystring::startswith(qualified, std::string("std::__1"))) {
        auto new_qualified = pystring::replace(
            qualified, std::string("std::__1::"), std::string("std::"));
        return Remapped{new_qualified, short_};
    }

    return Remapped{qualified, short_};
}
} // namespace libcpp

//------------------------------------------------------------------------------
// Entry point
//------------------------------------------------------------------------------
// Namespace
Remapped namespace_(StandardTemplateLib stl, const std::string& qualified,
                    const std::string& short_) {
    switch (stl) {
    case StandardTemplateLib::kstdcpp:
        return libcpp::namespace_(qualified, short_);
    // case StandardTemplateLib::kcpp:
    default:
        return Remapped{qualified, short_};
    }
}

// Record
Remapped record(StandardTemplateLib stl, const std::string& qualified,
                const std::string& short_) {
    switch (stl) {
    case StandardTemplateLib::kstdcpp:
        return libcpp::record(qualified, short_);
    // case StandardTemplateLib::kcpp:
    default:
        return Remapped{qualified, short_};
    }
}

} // namespace remap
} // namespace cppmm
