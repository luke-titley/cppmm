//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_stl_remap.hpp"

namespace cppmm {
namespace remap {

namespace libcpp {

//------------------------------------------------------------------------------
// libcpp/clang
//------------------------------------------------------------------------------
Remapped namespace_(const std::string& qualified, const std::string& short_) {

    return Remapped{qualified, short_};
}

Remapped record(const std::string& qualified, const std::string& short_) {
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
