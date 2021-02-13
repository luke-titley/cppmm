//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_ast_add_c.hpp"
#include "pystring.h"
#include <iostream>
#include <unordered_map>

#include <cstdlib> // for exit function

#define cassert(C, M) if(!(C)) { std::cerr << M << std::endl; abort(); }

namespace cppmm {
namespace transform {

//------------------------------------------------------------------------------
// RecordRegistry
//------------------------------------------------------------------------------
class RecordRegistry
{
    struct Records
    {
        NodePtr m_cpp;
        NodePtr m_c;
    };

    // The node entries are sparse, so store them in a map for the moment.
    using Mapping = std::unordered_map<NodeId, Records>;

    Mapping m_mapping;

public:
    void add(NodeId id, NodePtr cpp, NodePtr c)
    {
        // TODO LT: Assert for double entries
        // TODO LT: Assert for RecordKind in cpp and c
        m_mapping.insert(std::make_pair( id, Records{ std::move(cpp),
                                                      std::move(c)
                                              }
        ));
    }

    NodeRecord & edit_c(NodeId id)
    {
        auto & node = m_mapping[id].m_c; // TODO LT: Check existance + return optional
        // TODO LT: Assert kind is record

        return static_cast<NodeRecord&>(*node);
    }

    NodePtr find_c(NodeId id) const
    {
        auto entry = m_mapping.find(id);

        if (entry == m_mapping.end())
        {
            return NodePtr(); // TODO LT: Turn this into optional
        }
        else
        {
            return entry->second.m_c;
        }
    }
};

//------------------------------------------------------------------------------
namespace generate
{

const NodeId PLACEHOLDER_ID = 0;
const char * IGNORE = "cppmm:ignore";

//------------------------------------------------------------------------------
std::tuple<std::string, std::string>
             compute_c_filepath(const std::string & outdir,
                               const std::string & cpp_filepath)
{
    std::string root;
    std::string _ext;
    pystring::os::path::splitext(root, _ext,
                                 pystring::os::path::basename(cpp_filepath));
    
    return {root + ".h",
            pystring::os::path::join(outdir, root) + ".cpp"};
}

//------------------------------------------------------------------------------
std::string compute_c_name(const std::string & cpp_record_name)
{
    std::string result;
    for( auto const & c : cpp_record_name )
    {
        switch (c)
        {
            case ':':
            case ',':
            case '*':
            case '<':
            case '>':
                result.push_back('_');
                break;
            case ' ':
                break;
            default:
                result.push_back(c);
        }
    }
    return result;
}

NodeTypePtr convert_type(TranslationUnit & c_tu,
                         RecordRegistry & record_registry,
                         const NodeTypePtr & t, bool in_reference);

//------------------------------------------------------------------------------
NodeTypePtr convert_builtin_type(TranslationUnit & c_tu,
                                 RecordRegistry & record_registry,
                                 const NodeTypePtr & t, bool _in_refererence)
{
    // TODO LT: Do mapping of c++ builtins to c builtins

    // For now just copy everything one to one.
    return std::make_shared<NodeBuiltinType>(t->name, 0, t->type_name,
                                             t->const_);
}


//------------------------------------------------------------------------------
void add_declaration(TranslationUnit & c_tu, const NodePtr & node_ptr,
                     bool in_reference)
{
    const auto & record = *static_cast<const NodeRecord*>(node_ptr.get());
    if(auto r_tu = record.tu.lock())
    {
        if(r_tu.get() != &c_tu)
        {
            if(in_reference) // Forward declaration
            {
                c_tu.forward_decls.insert(node_ptr);
                c_tu.source_includes.insert(r_tu->header_filename);
            }
            else // Header file
            {
                c_tu.header_includes.insert(r_tu->header_filename);
            }
        }
    }
}

//------------------------------------------------------------------------------
NodeTypePtr convert_record_type(TranslationUnit & c_tu,
                                RecordRegistry & record_registry,
                                const NodeTypePtr & t, bool in_reference)
{
    const auto & cpp_record_type = *static_cast<const NodeRecordType*>(t.get());

    const auto & node_ptr = record_registry.find_c(cpp_record_type.record);
    if (!node_ptr)
    {
        std::cerr << "Found unsupported type: " << t->type_name << std::endl;
        return NodeTypePtr();
    }

    // Add the header file or forward declaration needed for this type
    // to be available.
    add_declaration(c_tu, node_ptr, in_reference);

    const auto & record = *static_cast<const NodeRecord*>(node_ptr.get());
    return std::make_shared<NodeRecordType>(t->name, 0, record.name,
                                            record.id, t->const_);
}

//------------------------------------------------------------------------------
NodeTypePtr convert_pointer_type(TranslationUnit & c_tu,
                                 RecordRegistry & record_registry,
                                 const NodeTypePtr & t, bool in_reference)
{
    auto p = static_cast<const NodePointerType*>(t.get());

    auto pointee_type =
        convert_type(c_tu, record_registry, p->pointee_type, true);

    // If we can't convert, then dont bother with this type either
    if(!pointee_type)
    {
        return NodeTypePtr();
    }

    // For now just copy everything one to one.
    return std::make_shared<NodePointerType>(p->name, 0, p->type_name,
                                             PointerKind::Pointer,
                                             std::move(pointee_type),
                                             p->const_);
}

//------------------------------------------------------------------------------
NodeTypePtr convert_type(TranslationUnit & c_tu,
                         RecordRegistry & record_registry,
                         const NodeTypePtr & t, bool in_reference=false)
{
    switch (t->kind)
    {
        case NodeKind::BuiltinType:
            return convert_builtin_type(c_tu, record_registry, t, in_reference);
        case NodeKind::RecordType:
            return convert_record_type(c_tu, record_registry, t, in_reference);
        case NodeKind::PointerType:
            return convert_pointer_type(c_tu, record_registry, t, in_reference);
        case NodeKind::EnumType:
            return NodeTypePtr(); // TODO LT: Enum translation
        default:
            break;
    }

    cassert(false, "convert_type: Shouldn't get here"); // TODO LT: Clean this up
}

//------------------------------------------------------------------------------
bool parameter(TranslationUnit & c_tu,
               RecordRegistry & record_registry,
               std::vector<Param> & params, const Param & param)
{
    auto param_type = convert_type(c_tu, record_registry, param.type);
    if(!param_type)
    {
        return false;
    }

    params.push_back(
            Param( std::string(param.name),
                   std::move(param_type),
                   params.size() )
    );

    return true;
}

//------------------------------------------------------------------------------
void opaquebytes_record(NodeRecord & c_record)
{
    auto is_const = false;
    auto array_type =\
        std::make_unique<NodeArrayType>(
            "", PLACEHOLDER_ID, "",
            std::make_unique<NodeBuiltinType>("", PLACEHOLDER_ID, "char",
                                              is_const),
            c_record.size, is_const);

    c_record.force_alignment = true;
    c_record.fields.push_back(Field{ "data", std::move(array_type) });
}

//------------------------------------------------------------------------------
Param self_param(const NodeRecord & c_record, bool const_)
{

    auto record = std::make_shared<NodeRecordType>(
                                                "",
                                                0,
                                                c_record.name,
                                                c_record.id,
                                                const_
                                           );

    auto pointer = std::make_shared<NodePointerType>("", 0,
                                           "",
                                           PointerKind::Pointer,
                                           std::move(record), false // TODO LT: Maybe references should be const pointers
                                           );

    return Param("self", std::move(pointer), 0);
}

//------------------------------------------------------------------------------
NodeExprPtr this_reference(const NodeRecord & cpp_record, bool const_)
{
    auto record = std::make_shared<NodeRecordType>(
                    "", 0, cpp_record.name, cpp_record.id, const_
    );
    auto type = std::make_shared<NodePointerType>(
                    "", 0, "", PointerKind::Pointer,
                    std::move(record), false 
    );
    auto self = std::make_shared<NodeVarRefExpr>("self");
    auto cast = std::make_shared<NodeCastExpr>(std::move(self),
                                               std::move(type),
                                               "reinterpret");

    return cast;
}

//------------------------------------------------------------------------------
bool should_wrap(const NodeMethod & cpp_method)
{
    // Check its not ignored
    for(const auto & a : cpp_method.attrs)
    {
        if(a == IGNORE)
        {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
NodeExprPtr convert_builtin_to(const NodeTypePtr & t, const NodeExprPtr & name)
{
#if 0
    auto type = NodeTypePtr(t);
    return std::make_shared<NodeCastExpr>(NodeExprPtr(name),
                                          std::move(type),
                                          "static");
#else
    return NodeExprPtr(name);
#endif
}

//------------------------------------------------------------------------------
NodeExprPtr convert_record_to(const NodeTypePtr & t, const NodeExprPtr & name)
{
    // TODO LT: Assuming opaquebytes at the moment, opaqueptr will have a
    // different implementation.
    //
    auto reference = std::make_shared<NodeRefExpr>(NodeExprPtr(name));
    auto type =\
        std::make_shared<NodePointerType>(
            "", 0, "", PointerKind::Pointer,
            std::move(NodeTypePtr(t)),
            false
    );
    auto inner = std::make_shared<NodeCastExpr>(
        std::move(reference), std::move(type), "reinterpret");
    return std::make_shared<NodeDerefExpr>(std::move(inner));

    /*
    // Above code could look like this later.
    return DerefExpr::n(
        CastExpr::n(
            RefExpr::n(
                VarRefExpr::n( name ) ),
            PointerType::n( t, false ))
    )
    */
}

//------------------------------------------------------------------------------
NodeExprPtr convert_pointer_to(const NodeTypePtr & t, const NodeExprPtr & name)
{
    // TODO LT: Assuming opaquebytes at the moment, opaqueptr will have a
    // different implementation.
    //
    auto p = static_cast<const NodePointerType*>(t.get());

    switch (p->pointer_kind)
    {
        case PointerKind::Pointer:
            {
                auto type = NodeTypePtr(t);
                return std::make_shared<NodeCastExpr>(NodeExprPtr(name),
                                                      std::move(type),
                                                      "reinterpret");
            }
        case PointerKind::RValueReference: // TODO LT: Add support for rvalue reference
        case PointerKind::Reference:
            {
                auto pointee = NodeTypePtr(p->pointee_type);
                auto type =\
                    std::make_shared<NodePointerType>(
                        "", 0, "", PointerKind::Pointer,
                        std::move(pointee),
                        p->const_
                );
                auto inner = std::make_shared<NodeCastExpr>(
                    NodeExprPtr(name), std::move(type), "reinterpret");
                return std::make_shared<NodeDerefExpr>(std::move(inner));
            }
        default:
            break;
    }
    
    cassert(false, "convert_pointer_arg Shouldn't get here"); // TODO LT: Clean this up
}

//------------------------------------------------------------------------------
NodeExprPtr convert_to(const NodeTypePtr & t, const NodeExprPtr & name)
{
    switch (t->kind)
    {
        case NodeKind::BuiltinType:
            return convert_builtin_to(t, name);
        case NodeKind::RecordType:
            return convert_record_to(t, name);
        case NodeKind::PointerType:
            return convert_pointer_to(t, name);
        default:
            break;
    }

    cassert(false, "convert_to Shouldn't get here"); // TODO LT: Clean this up
}

//------------------------------------------------------------------------------
NodeExprPtr convert_builtin_from(
                                 const NodeTypePtr & from_ptr,
                                 const NodeTypePtr & to_ptr,
                                 const NodeExprPtr & name)
{
    // TODO LT: Be smarter
    return NodeExprPtr(name);
}

//------------------------------------------------------------------------------
NodeExprPtr convert_record_from(
                                 const NodeTypePtr & from_ptr,
                                 const NodeTypePtr & to_ptr,
                                 const NodeExprPtr & name)
{
    auto reference = std::make_shared<NodeRefExpr>(NodeExprPtr(name));
    auto inner = std::make_shared<NodeCastExpr>(
        std::move(reference), NodeTypePtr(to_ptr), "reinterpret");
    return std::make_shared<NodeDerefExpr>(std::move(inner));
}

//------------------------------------------------------------------------------
NodeExprPtr convert_pointer_from(
                                 const NodeTypePtr & from_ptr,
                                 const NodeTypePtr & to_ptr,
                                 const NodeExprPtr & name)
{
    // TODO LT: Assuming opaquebytes at the moment, opaqueptr will have a
    // different implementation.
    //
    auto from = static_cast<const NodePointerType*>(from_ptr.get());

    switch (from->pointer_kind)
    {
        case PointerKind::Pointer:
            {
                return std::make_shared<NodeCastExpr>(NodeExprPtr(name),
                                                      NodeTypePtr(to_ptr),
                                                      "reinterpret");
            }
        case PointerKind::RValueReference: // TODO LT: Add support for rvalue reference
        case PointerKind::Reference:
            {
                auto ref = std::make_shared<NodeRefExpr>(NodeExprPtr(name));
                return std::make_shared<NodeCastExpr>(
                    NodeExprPtr(name), NodeTypePtr(to_ptr), "reinterpret");
            }
        default:
            break;
    }
    
    cassert(false, "convert_pointer_arg Shouldn't get here"); // TODO LT: Clean this up
}

//------------------------------------------------------------------------------
NodeExprPtr convert_from(const NodeTypePtr & from,
                         const NodeTypePtr & to,
                         const NodeExprPtr & name)
{
    switch (to->kind)
    {
        case NodeKind::BuiltinType:
            return convert_builtin_from(from, to, name);
        case NodeKind::RecordType:
            return convert_record_from(from, to, name);
        case NodeKind::PointerType:
            return convert_pointer_from(from, to, name);
        default:
            break;
    }

    cassert(false, "convert_to Shouldn't get here"); // TODO LT: Clean this up
}

//------------------------------------------------------------------------------
void argument(std::vector<NodeExprPtr> & args, const Param & param)
{
    auto argument =
        convert_to(param.type,
                   std::make_shared<NodeVarRefExpr>(param.name));
    args.push_back(argument);
}

//------------------------------------------------------------------------------
NodeExprPtr opaquebytes_constructor_body(RecordRegistry & record_registry,
                                         TranslationUnit & c_tu,
                                         const NodeRecord & cpp_record,
                                         const NodeRecord & c_record,
                                         const NodeMethod & cpp_method)
{
    // Loop over the parameters, creating arguments for the method call
    auto args = std::vector<NodeExprPtr>();
    for(const auto & p : cpp_method.params)
    {
        argument(args, p);
    }

    // Create the method call expression
    return std::make_shared<NodePlacementNewExpr>(
        std::make_shared<NodeVarRefExpr>("self"),
        std::make_shared<NodeFunctionCallExpr>(cpp_method.short_name,
                                               args
        )
    );
}

//------------------------------------------------------------------------------
NodeExprPtr opaquebytes_method_body(RecordRegistry & record_registry,
                                    TranslationUnit & c_tu,
                                    const NodeRecord & cpp_record,
                                    const NodeRecord & c_record,
                                    const NodeTypePtr & c_return,
                                    const NodeMethod & cpp_method)
{
    // Create the reference to this
    auto this_ = this_reference(cpp_record, false); // TODO LT: Missing cpp_method.const_

    // Loop over the parameters, creating arguments for the method call
    auto args = std::vector<NodeExprPtr>();
    for(const auto & p : cpp_method.params)
    {
        argument(args, p);
    }

    // Create the method call expression
    auto method_call =
        std::make_shared<NodeMethodCallExpr>(std::move(this_),
                                             cpp_method.short_name,
                                             args
    );

    // Convert the result
    auto is_void =
        c_return->kind == NodeKind::BuiltinType &&
        static_cast<const NodeBuiltinType*>(c_return.get())->type_name == "void";

    if(is_void)
    {
        return method_call;
    }
    else
    {
        return std::make_shared<NodeReturnExpr>(
            convert_from(cpp_method.return_type, c_return, method_call));
    }
    //return method_call;
}

//------------------------------------------------------------------------------
NodeExprPtr opaquebytes_c_function_body(RecordRegistry & record_registry,
                                        TranslationUnit & c_tu,
                                        const NodeRecord & cpp_record,
                                        const NodeRecord & c_record,
                                        const NodeTypePtr & c_return,
                                        const NodeMethod & cpp_method)
{
    if(cpp_method.is_constructor)
    {
        return opaquebytes_constructor_body(
            record_registry, c_tu, cpp_record, c_record, cpp_method);
    }
    else
    {
        return opaquebytes_method_body(
            record_registry, c_tu, cpp_record, c_record, c_return, cpp_method);
    }
}

//------------------------------------------------------------------------------
void opaquebytes_method(RecordRegistry & record_registry,
                        TranslationUnit & c_tu,
                        const NodeRecord & cpp_record,
                        const NodeRecord & c_record,
                        const NodeMethod & cpp_method)
{
    // Skip ignored methods
    if(!should_wrap(cpp_method))
    {
        std::cerr << "ignoring method decl: " << cpp_method.name << std::endl;
        return;
    }

    // Convert params
    auto c_params = std::vector<Param>();
    c_params.push_back(self_param(c_record, false)); // TODO LT: Const needs to be passed in here
    for(const auto & p : cpp_method.params)
    {
        if(!parameter(c_tu, record_registry, c_params, p))
        {
            std::cerr << "Skipping: " << cpp_method.name << std::endl;
            return;
        }
    }

    // Convert return type
    auto c_return = convert_type(c_tu, record_registry, cpp_method.return_type);
    if(!c_return)
    {
        std::cerr << "Skipping: " << cpp_method.name << std::endl;
        return;
    }

    // Function body
    auto c_function_body =
        opaquebytes_c_function_body(record_registry, c_tu, cpp_record, c_record,
                                    c_return, cpp_method);

    // Add the new function to the translation unit
    auto c_function = std::make_shared<NodeFunction>(
                        compute_c_name(cpp_method.name), PLACEHOLDER_ID,
                        cpp_method.attrs, "", std::move(c_return),
                        std::move(c_params));

    c_function->body = c_function_body;

    c_tu.decls.push_back(std::move(c_function));
}

//------------------------------------------------------------------------------
void opaquebytes_methods(RecordRegistry & record_registry,
                         TranslationUnit & c_tu, const NodeRecord & cpp_record,
                         const NodeRecord & c_record)
{
    for(const auto & m: cpp_record.methods)
    {
        opaquebytes_method(record_registry, c_tu, cpp_record, c_record, m);
    }
}

//------------------------------------------------------------------------------
void record_entry(NodeId & record_id,
                  RecordRegistry & record_registry, TranslationUnit::Ptr & c_tu,
                  const NodePtr & cpp_node)
{
    const auto & cpp_record =\
        *static_cast<const NodeRecord*>(cpp_node.get());

    const auto c_record_name = compute_c_name(cpp_record.name);

    // Create the c record
    auto c_record =\
        std::make_shared<NodeRecord>(
                   c_tu,
                   c_record_name, record_id++, cpp_record.attrs,
                   cpp_record.size, cpp_record.align);

    // Add the cpp and c record to the registry
    record_registry.add(cpp_node->id, cpp_node, c_record);

    // Finally add the record to the translation unit
    c_tu->decls.push_back(std::move(c_record));
}

//------------------------------------------------------------------------------
void record_detail(RecordRegistry & record_registry, TranslationUnit & c_tu,
                   const NodePtr & cpp_node)
{
    const auto & cpp_record =\
        *static_cast<NodeRecord*>(cpp_node.get());

    // Most simple record implementation is the opaque bytes.
    // Least safe and most restrictive in use, but easiest to implement.
    // So doing that first. Later will switch depending on the cppm attributes.
    auto & c_record = record_registry.edit_c(cpp_record.id); // TODO LT: Return optional for error
    opaquebytes_record(c_record);

    // Most simple record implementation is the opaque bytes.
    // Least safe and most restrictive in use, but easiest to implement.
    // So doing that first. Later will switch depending on the cppm attributes.

    opaquebytes_methods(record_registry, c_tu, cpp_record, c_record);
}

//------------------------------------------------------------------------------
NodeId find_record_id_upper_bound(const Root & root)
{
    // Loop through all the record ids and find the largest one.
    // This will be used as the starting point for creating new ids.
    NodeId upper_bound = 0;
    for(const auto & t: root.tus)
    {
        for (const auto & node : t->decls)
        {
            if (node->kind == NodeKind::Record)
            {
                upper_bound = std::max(upper_bound, node->id);
            }
        }
    }

    return upper_bound;
}

//------------------------------------------------------------------------------
std::string header_file_include(std::string header_filename)
{
    std::string result = "#include <";
    result += header_filename;
    result += ">";
    return result;
}

//------------------------------------------------------------------------------
void translation_unit_entries(
    NodeId & new_record_id,
    RecordRegistry & record_registry,
    const std::string & output_directory, Root & root,
    const size_t cpp_tu_index)
{
    const auto & cpp_tu = root.tus[cpp_tu_index];

    // Create a new translation unit
    const auto filepaths =
        generate::compute_c_filepath(output_directory,
                                     cpp_tu->filename);

    // Make the new translation unit
    auto c_tu = TranslationUnit::new_(std::get<1>(filepaths));
    c_tu->header_filename = header_file_include(std::get<0>(filepaths));

    // source includes -> source includes
    for (auto & i : cpp_tu->source_includes)
    {
        c_tu->source_includes.insert(i);
    }


    // cpp records -> c records
    for (const auto & node : cpp_tu->decls)
    {
        if (node->kind == NodeKind::Record)
        {
            generate::record_entry(new_record_id, record_registry, c_tu, node);
        }
    }

    root.tus.push_back(std::move(c_tu));
}

//------------------------------------------------------------------------------
void translation_unit_details(
    RecordRegistry & record_registry,
    Root & root, const size_t cpp_tu_size, const size_t cpp_tu)
{
    auto & c_tu = *root.tus[cpp_tu_size+cpp_tu];

    // cpp methods -> c functions
    for (const auto & node : root.tus[cpp_tu]->decls)
    {
        if (node->kind == NodeKind::Record)
        {
            generate::record_detail(record_registry, c_tu, node);
        }
    }
}

} // namespace generate


//------------------------------------------------------------------------------
void add_c(const std::string & output_directory, Root & root)
{
    // For storing the mappings between cpp and c records
    auto record_registry = RecordRegistry();

    // The starting id for newly created records
    NodeId current_record_id = generate::find_record_id_upper_bound(root) + 1;

    // When we iterate we dont want to loop over newly added c translation units
    const auto tu_count = root.tus.size();
    for (size_t i=0; i != tu_count; ++i)
    {
        generate::translation_unit_entries(
            current_record_id,
            record_registry, output_directory, root, i);
    }

    // Implement the records
    for (size_t i=0; i != tu_count; ++i)
    {
        generate::translation_unit_details(record_registry, root, tu_count, i);
    }
}

} // namespace transform
} // namespace cppmm
