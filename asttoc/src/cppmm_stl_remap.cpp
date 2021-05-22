//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_stl_remap.hpp"

namespace cppmm {
namespace remap {

// Namespace
std::string namespace_qualified(StandardTemplateLib stl,
                                const std::string& val) {
    return val;
}

std::string namespace_short(StandardTemplateLib stl, const std::string& val) {
    return val;
}

// Record
std::string record_qualified(StandardTemplateLib stl, const std::string& val) {
    return val;
}

std::string record_short(StandardTemplateLib stl, const std::string& val) {
    return val;
}

} // namespace remap
} // namespace cppmm
