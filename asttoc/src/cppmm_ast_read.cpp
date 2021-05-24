
//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_ast_read.hpp"
#include "base64.hpp"
#include "cppmm_stl_remap.hpp"
#include "filesystem.hpp"
#include "json.hh"

#include <iostream>
#include <memory>

#define SPDLOG_ACTIVE_LEVEL TRACE

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#define panic(...)                                                             \
    {                                                                          \
        SPDLOG_CRITICAL(__VA_ARGS__);                                          \
        abort();                                                               \
    }

#define expect(CONDITION, ...)                                                 \
    if (!(CONDITION)) {                                                        \
        SPDLOG_CRITICAL(__VA_ARGS__);                                          \
        abort();                                                               \
    }

namespace fs = ghc::filesystem;

namespace nln = nlohmann;

namespace cppmm {
namespace read {

namespace {
const char* ALIGN = "align";
const char* ALIAS = "alias";
const char* ATTRIBUTES = "attributes";
const char* ABSTRACT = "abstract";
const char* CHILDREN = "children";
const char* DESTRUCTOR = "destructor";
const char* COLLAPSE = "collapse";
const char* COMMENT = "comment";
const char* CONSTRUCTOR = "constructor";
const char* COPY_CONSTRUCTOR = "copy_constructor";
const char* MOVE_CONSTRUCTOR = "move_constructor";
const char* CONST = "const";
const char* DECLS = "decls";
const char* ENUM_C = "Enum";
const char* ENUM_L = "enum";
const char* EXCEPTIONS = "exceptions";
const char* FIELDS = "fields";
const char* FILENAME = "filename";
const char* FUNCTION_C = "Function";
const char* ID = "id";
const char* INDEX = "index";
const char* KIND = "kind";
const char* METHODS = "methods";
const char* NAME = "name";
const char* NAMESPACE_C = "Namespace";
const char* NAMESPACES = "namespaces";
const char* QUALIFIED_NAME = "qualified_name";
const char* SHORT_NAME = "short_name";
const char* PARAMS = "params";
const char* SIZE = "size";
const char* STATIC = "static";
const char* RECORD_C = "Record";
const char* RECORD_L = "record";
const char* TYPE = "type";
const char* TRIVIALLY_COPYABLE = "trivially_copyable";
const char* TRIVIALLY_MOVABLE = "trivially_movable";
const char* POINTEE = "pointee";
const char* RETURN = "return";
const char* SOURCE_INCLUDES = "source_includes";
const char* INCLUDE_PATHS = "include_paths";
const char* VARIANTS = "variants";
const char* VAR_C = "Var";
const char* ELEMENT_TYPE = "element_type";
const char* FUNCTION_POINTER_TYPEDEF_C = "FunctionPointerTypedef";
const char* FUNCTION_POINTER_TYPEDEF_L = "function_pointer_typedef";
const char* STL = "standard_template_library";
const char* TEMPLATE_ARGS = "template_args";
const char* OPAQUE_TYPE = "opaque_type";
const char* NOEXCEPT = "noexcept";
} // namespace

struct CurrentObject {
    StandardTemplateLib stl;
    const nln::json& json;
};

//------------------------------------------------------------------------------
NodeTypePtr read_type(const nln::json& json);

//------------------------------------------------------------------------------
NodeTypePtr read_type_builtin(const nln::json& json) {
    return NodeBuiltinType::n("", json[ID].get<Id>(),
                              json[TYPE].get<std::string>(),
                              json[CONST].get<bool>());
}

//------------------------------------------------------------------------------
NodeTypePtr read_type_pointer(const nln::json& json, PointerKind pointer_kind) {
    return NodePointerType::n(pointer_kind, read_type(json[POINTEE]),
                              json[CONST].get<bool>());
}

//------------------------------------------------------------------------------
NodeTypePtr read_type_record(const nln::json& json) {
    return NodeRecordType::n(
        "", json[ID].get<Id>(), json[TYPE].get<std::string>(),
        json[RECORD_L].get<NodeId>(), json[CONST].get<bool>());
}

//------------------------------------------------------------------------------
NodeTypePtr read_type_enum(const nln::json& json) {
    return NodeEnumType::n("", json[ID].get<Id>(),
                           json[TYPE].get<std::string>(),
                           json[ENUM_L].get<NodeId>(), json[CONST].get<bool>());
}

//------------------------------------------------------------------------------
NodeTypePtr read_type_const_array(const nln::json& json) {
    return NodeArrayType::n("", json[ID].get<NodeId>(),
                            json[TYPE].get<std::string>(),
                            read_type(json[ELEMENT_TYPE]),
                            json[SIZE].get<int>(), json[CONST].get<bool>());
}

//------------------------------------------------------------------------------
NodeTypePtr read_type_unknown(const nln::json& json) {
    return NodeUnknownType::n(json[CONST].get<bool>());
}

NodeTypePtr read_type_function_proto(const nln::json& json);

//------------------------------------------------------------------------------
NodeTypePtr read_type(const nln::json& json) {

    auto kind_iter = json.find(KIND);
    if (kind_iter != json.end()) {
        auto kind = json[KIND].get<std::string>();
        if (kind == "BuiltinType") {
            return read_type_builtin(json);
        } else if (kind == "RecordType") {
            return read_type_record(json);
        } else if (kind == "Reference") {
            return read_type_pointer(json, PointerKind::Reference);
        } else if (kind == "RValueReference") {
            return read_type_pointer(json, PointerKind::RValueReference);
        } else if (kind == "Pointer") {
            return read_type_pointer(json, PointerKind::Pointer);
        } else if (kind == "EnumType") {
            return read_type_enum(json);
        } else if (kind == "FunctionProtoType") {
            return read_type_function_proto(json);
        } else if (kind == "ConstantArrayType") {
            return read_type_const_array(json);
        }

        SPDLOG_CRITICAL("Unhandled type kind {}", kind);
    } else {
        auto type_iter = json.find(TYPE);
        if (type_iter != json.end() &&
            type_iter->get<std::string>() == "UNKNOWN") {
            return read_type_unknown(json);
        } else {
            SPDLOG_CRITICAL("Could not find {} in json", TYPE);
        }
    }

    panic("{}", json);
}

//------------------------------------------------------------------------------
Param read_param(const nln::json& json) {

    auto name = json[NAME].get<std::string>();
    auto type = read_type(json[TYPE]);
    auto index = json[INDEX].get<uint64_t>();

    return Param(std::move(name), std::move(type), index);
}

//------------------------------------------------------------------------------
std::vector<std::string> read_attrs(const nln::json& json) {
    auto attrs = std::vector<std::string>();
    for (const auto& i : json[ATTRIBUTES]) {
        attrs.push_back(i.get<std::string>());
    }
    return attrs;
}

//------------------------------------------------------------------------------
std::vector<Exception> read_exceptions(const nln::json& json) {
    auto result = std::vector<Exception>();
    for (const auto& e : json[EXCEPTIONS]) {
        auto cpp_name = e["cpp_name"].get<std::string>();
        auto c_name = e["c_name"].get<std::string>();
        auto error_code = e["error_code"].get<unsigned int>();
        result.push_back(Exception{cpp_name, c_name, error_code});

        // FIXME: will we ever multithread reading? This would be bad
        EXCEPTION_MAP[error_code] = c_name;
    }
    return result;
}

//------------------------------------------------------------------------------
std::string read_comment(const nln::json& json) {
    std::string comment;
    auto comment_ = json.find(COMMENT);
    if (comment_ != json.end()) {
        comment = base64::decode(comment_->get<std::string>());
    }

    return comment;
}

//------------------------------------------------------------------------------
NodeTypePtr read_type_function_proto(const nln::json& json) {
    auto return_type = read_type(json[RETURN]);

    auto params = std::vector<NodeTypePtr>();
    for (const auto& i : json[PARAMS]) {
        auto p = read_param(i);
        params.push_back(std::move(p.type));
    }

    return NodeFunctionProtoType::n(
        std::move(return_type), std::move(params),
        json[TYPE].get<std::string>(),
        json[FUNCTION_POINTER_TYPEDEF_L].get<NodeId>());
}

//------------------------------------------------------------------------------
NodePtr read_function(const TranslationUnit::Ptr& tu, const CurrentObject co) {
    auto json = co.json;

    // ignore for the moment
    auto attrs = read_attrs(json);

    auto qualified_name = json[QUALIFIED_NAME].get<std::string>();

    auto short_name = json[SHORT_NAME].get<std::string>();
    auto id = json[ID].get<Id>();
    auto return_type = read_type(json[RETURN]);

    auto params = std::vector<Param>();
    for (const auto& i : json[PARAMS]) {
        params.push_back(read_param(i));
    }

    // Namespaces
    std::vector<NodeId> namespaces;
    for (const auto& ns : json[NAMESPACES]) {
        namespaces.push_back(ns);
    }

    auto exceptions = read_exceptions(json);
    auto is_noexcept = json[NOEXCEPT].get<bool>();

    auto template_args = std::vector<NodeTypePtr>();
    for (const auto& i : json[TEMPLATE_ARGS]) {
        auto typ = read_type(i[TYPE]);
        SPDLOG_DEBUG("Read template arg type {}", typ->type_name);
        template_args.push_back(typ);
    }

    auto comment = read_comment(json);

    auto result = NodeFunction::n(
        qualified_name, id, attrs, short_name, std::move(return_type),
        std::move(params), qualified_name, std::move(comment),
        std::move(template_args), std::move(exceptions), is_noexcept);
    result->namespaces = namespaces;

    return result;
}

//------------------------------------------------------------------------------
NodeMethod read_method(const nln::json& json) {

    auto qualified_name = json[QUALIFIED_NAME].get<std::string>();
    auto attrs = read_attrs(json);

    auto short_name = json[SHORT_NAME].get<std::string>();
    auto id = json[ID].get<Id>();
    auto static_ = json[STATIC].get<bool>();
    auto constructor = json[CONSTRUCTOR].get<bool>();
    auto destructor = json[DESTRUCTOR].get<bool>();
    auto copy_constructor = json[COPY_CONSTRUCTOR].get<bool>();
    auto move_constructor = json[MOVE_CONSTRUCTOR].get<bool>();
    auto return_type = read_type(json[RETURN]);
    auto const_ = json[CONST].get<bool>();
    auto comment = read_comment(json);

    if (copy_constructor) {
        SPDLOG_DEBUG("Reading copy {}", short_name);
    }
    /*
        if(constructor){
            SPDLOG_WARN("Reading constructor {}", short_name);
        }
    */

    auto params = std::vector<Param>();
    for (const auto& i : json[PARAMS]) {
        params.push_back(read_param(i));
    }

    auto template_args = std::vector<NodeTypePtr>();
    for (const auto& i : json[TEMPLATE_ARGS]) {
        auto typ = read_type(i[TYPE]);
        SPDLOG_DEBUG("Read template arg type {}", typ->type_name);
        template_args.push_back(typ);
    }

    auto exceptions = read_exceptions(json);
    auto is_noexcept = json[NOEXCEPT].get<bool>();

    return NodeMethod(
        qualified_name, id, attrs, short_name, std::move(return_type),
        std::move(params), static_, constructor, copy_constructor,
        move_constructor, destructor, const_, std::move(comment),
        std::move(template_args), std::move(exceptions), is_noexcept);
}

//------------------------------------------------------------------------------
Field read_field(const nln::json& json) {
    return Field{
        std::move(json[NAME].get<std::string>()),
        std::move(read_type(json[TYPE])),
    };
}

//------------------------------------------------------------------------------
NodePtr read_record(const TranslationUnit::Ptr& tu, const CurrentObject co) {
    // Get the json object
    auto json = co.json;

    // Dont ignore these
    Id id = json[ID].get<Id>();
    auto size = json[SIZE].get<uint64_t>();
    auto align = json[ALIGN].get<uint64_t>();
    auto name = remap::record(co.stl, json[NAME].get<std::string>(),
                              json[SHORT_NAME].get<std::string>());

    // Find if abstract
    auto abstract = false;
    auto abstract_a = json.find(ABSTRACT);
    if (abstract_a != json.end()) {
        abstract = abstract_a->get<bool>();
    }

    // Find if trivially_copyable
    auto trivially_copyable = false;
    auto trivially_copyable_a = json.find(TRIVIALLY_COPYABLE);
    if (trivially_copyable_a != json.end()) {
        trivially_copyable = trivially_copyable_a->get<bool>();
    }

    auto trivially_movable = false;
    auto trivially_movable_a = json.find(TRIVIALLY_MOVABLE);
    if (trivially_movable_a != json.end()) {
        trivially_movable = trivially_movable_a->get<bool>();
    }

    auto opaque_type = false;
    auto opaque_type_a = json.find(OPAQUE_TYPE);
    if (opaque_type_a != json.end()) {
        opaque_type = opaque_type_a->get<bool>();
    }

    // Override the name with an alias if one is provided
    auto alias = json.find(ALIAS);
    if (alias != json.end()) {
        name.short_ = alias->get<std::string>();
    }

    // Namespaces
    std::vector<NodeId> namespaces;
    for (const auto& ns : json[NAMESPACES]) {
        namespaces.push_back(ns);
    }

    // Pull out the attributes
    std::vector<std::string> attrs = read_attrs(json);

    // Read the comment
    auto comment = read_comment(json);

    // Instantiate the translation unit
    auto result =
        NodeRecord::n(tu, name.qualified, id, attrs, size, align, name.short_,
                      namespaces, abstract, trivially_copyable,
                      trivially_movable, opaque_type, std::move(comment));

    // Pull out the methods
    for (const auto& i : json[METHODS]) {
        result->methods.push_back(read_method(i));
    }

    // Pull out the fields
    for (const auto& i : json[FIELDS]) {
        result->fields.push_back(std::move(read_field(i)));
    }

    // Return the result
    return result;
}

struct sort_enum_vars {
    bool operator()(const std::pair<std::string, std::string>& a,
                    const std::pair<std::string, std::string>& b) {
        return atoi(a.second.c_str()) < atoi(b.second.c_str());
    }
};

//------------------------------------------------------------------------------
NodePtr read_enum(const TranslationUnit::Ptr& tu, CurrentObject co) {
    auto json = co.json;

    Id id = json[ID].get<Id>();
    auto size = json[SIZE].get<uint64_t>();
    auto align = json[ALIGN].get<uint64_t>();
    auto name = json[NAME].get<std::string>();
    auto short_name = json[SHORT_NAME].get<std::string>();

    std::vector<NodeId> namespaces;
    for (const auto& ns : json[NAMESPACES]) {
        namespaces.push_back(ns);
    }

    std::vector<std::string> attrs = read_attrs(json);

    // Pull out the variants
    std::vector<std::pair<std::string, std::string>> variants;
    for (const auto& i : json[VARIANTS].items()) {
        variants.push_back(std::make_pair(
            i.key(),
            i.value().get<std::string>() // TODO LT: Check with anders, can
                                         // value be long int?
            ));
    }

    std::sort(variants.begin(), variants.end(), sort_enum_vars{});

    auto comment = read_comment(json);

    // Instantiate the translation unit
    auto result = NodeEnum::n(tu, name, name, short_name, id, attrs, variants,
                              size, align, namespaces, std::move(comment));

    // Return the result
    return result;
}

//------------------------------------------------------------------------------
NodePtr read_var(const TranslationUnit::Ptr& tu, const CurrentObject co) {
    auto json = co.json;

    // Ignore these for the moment
    std::vector<std::string> _attrs;

    // Dont ignore these
    auto name = json[SHORT_NAME].get<std::string>();
    // std::cout << "read type" << std::endl;
    auto type = read_type(json[TYPE]);

    // Instantiate the translation unit
    auto result = NodeVarDeclExpr::n(type, name);

    // Return the result
    return result;
}

//------------------------------------------------------------------------------
NodePtr read_namespace(const TranslationUnit::Ptr& tu, const CurrentObject co) {
    auto json = co.json;

    // Ignore these for the moment
    std::vector<std::string> _attrs;

    // Dont ignore these
    Id id = json[ID].get<Id>();
    auto name = remap::namespace_(co.stl, json[NAME].get<std::string>(),
                                  json[SHORT_NAME].get<std::string>());

    auto alias = json[ALIAS].get<std::string>();
    auto collapse = json[COLLAPSE].get<bool>();

    auto result =
        NodeNamespace::n(name.qualified, id, name.short_, alias, collapse);

    // Return the result
    return result;
}

//------------------------------------------------------------------------------
NodePtr read_function_pointer_typedef(const TranslationUnit::Ptr& tu,
                                      const CurrentObject co) {
    auto json = co.json;

    // Ignore these for the moment
    std::vector<std::string> _attrs;

    // Dont ignore these
    Id id = json[ID].get<Id>();
    auto name = json[NAME].get<std::string>();
    auto alias = json[ALIAS].get<std::string>();

    std::vector<NodeId> namespaces;
    for (const auto& ns : json[NAMESPACES]) {
        namespaces.push_back(ns);
    }

    auto comment = read_comment(json);

    auto return_type = read_type(json[RETURN]);

    auto params = std::vector<Param>();
    for (const auto& i : json[PARAMS]) {
        params.push_back(read_param(i));
    }

    auto result = NodeFunctionPointerTypedef::n(
        tu, name, id, alias, namespaces, std::move(comment),
        std::move(return_type), std::move(params));

    // Return the result
    return result;
}

//------------------------------------------------------------------------------
NodePtr read_node(const TranslationUnit::Ptr& tu, const CurrentObject co) {
    auto kind = co.json[KIND].get<std::string>();

    if (kind == RECORD_C) {
        return read_record(tu, co);
    } else if (kind == ENUM_C) {
        return read_enum(tu, co);
    } else if (kind == FUNCTION_C) {
        return read_function(tu, co);
    } else if (kind == NAMESPACE_C) {
        return read_namespace(tu, co);
    } else if (kind == VAR_C) {
        return read_var(tu, co);
    } else if (kind == FUNCTION_POINTER_TYPEDEF_C) {
        return read_function_pointer_typedef(tu, co);
    }

    panic("Unhandled node kind {}", kind);

    // TODO LT: Fix the return type
}

//------------------------------------------------------------------------------
StandardTemplateLib read_standard_template_lib(const nln::json& json) {
    auto stl = StandardTemplateLib::kstdcpp;
    std::string stl_str;
    auto stl_ = json.find(STL);
    if (stl_ != json.end()) {

        // stdcpp
        if (stl_->get<std::string>() == "stdcpp") {
            stl = StandardTemplateLib::kstdcpp;
        }

        // cpp
        else if (stl_->get<std::string>() == "cpp") {
            stl = StandardTemplateLib::kcpp;
        }
    }

    return stl;
}

//------------------------------------------------------------------------------
TranslationUnit::Ptr read_translation_unit(const nln::json& json) {
    // Read the translation unit
    auto filename = json[FILENAME].get<std::string>();

    // Read the standard template library type
    auto stl = read_standard_template_lib(json);

    // Instantiate the translation unit
    auto result = TranslationUnit::new_(filename);

    // Loop over the source includes
    for (auto& i : json[SOURCE_INCLUDES]) {
        result->source_includes.insert(i.get<std::string>());
    }

    // Loop over the include directories
    for (auto& i : json[INCLUDE_PATHS]) {
        result->include_paths.push_back(i.get<std::string>());
    }

    // Parse the elements of the translation unit
    for (const auto& i : json[DECLS]) {
        auto co = CurrentObject{stl, i};
        result->decls.push_back(read_node(result, co));
    }

    // Return the result
    return result;
}

//------------------------------------------------------------------------------
Root json(const std::string& input_directory) {

    std::vector<TranslationUnit::Ptr> tus;

    for (const auto& p : fs::directory_iterator(input_directory)) {
        if (p.path().extension() == ".json") {
            // Open the json file
            std::ifstream input_file;
            input_file.open(p.path());

            // Convert the input stream into json structures
            nln::json json;
            input_file >> json;

            // Later this can be a loop taking in multiple translation units
            tus.push_back(read_translation_unit(json));
        }
    }

    return Root(std::move(tus));
}

} // namespace read
} // namespace cppmm
