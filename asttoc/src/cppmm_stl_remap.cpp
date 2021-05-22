//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_stl_remap.hpp"

namespace cppmm {
namespace remap {

// Namespace
Remapped namespace_(StandardTemplateLib stl, const std::string& qualified,
                    const std::string& short_) {

    return Remapped{qualified, short_};
}

// Record
Remapped record(StandardTemplateLib stl, const std::string& qualified,
                const std::string& short_) {
    return Remapped{qualified, short_};
}

} // namespace remap
} // namespace cppmm
