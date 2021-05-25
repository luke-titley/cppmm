//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_stl_remap.hpp"
#include "pystring.h"
#include <iostream>

// _LIBCPP_VERSION = libc++
// __GLIBCXX__ = libstdc++

namespace cppmm {
namespace remap {

namespace libcpp {

//------------------------------------------------------------------------------
// libcpp/clang
//------------------------------------------------------------------------------
Remapped namespace_(const std::string& qualified, const std::string& short_) {

    //std::cerr << "namespace info " << qualified << std::endl;
    if (qualified == "std::__1") {
        return Remapped{"std", ""};
    }

    return Remapped{qualified, short_};
}

Remapped record(const std::string& qualified, const std::string& short_) {
    if (pystring::startswith(qualified, std::string("std::__1"))) {
        auto new_qualified =
            pystring::replace(qualified,
                              std::string("std::__1::"),
                              std::string("std::")
            );
        std::cerr << "    record info " << qualified << std::endl;
        std::cerr << "new record info " << new_qualified << std::endl;
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
