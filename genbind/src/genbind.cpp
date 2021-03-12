#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <unordered_map>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "llvm/Support/Casting.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>

#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include "filesystem.hpp"
#include "pystring.h"

#define SPDLOG_ACTIVE_LEVEL TRACE

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cassert>

using namespace clang::tooling;
using namespace llvm;
using namespace clang;
using namespace clang::ast_matchers;

namespace ps = pystring;
namespace fs = ghc::filesystem;

#include <nlohmann/json.hpp>
using json = nlohmann::ordered_json;

namespace cppmm {
/// Enumerates the kinds of nodes in the output AST
enum class NodeKind : uint32_t {
    Node = 0,
    TranslationUnit,
    Namespace,
    BuiltinType,
    PointerType,
    RecordType,
    EnumType,
    FunctionProtoType,
    Parm,
    Function,
    Method,
    Record,
    Enum,
    ConstantArrayType,
    Var,
    TemplateTypeParmType,
};

std::ostream& operator<<(std::ostream& os, NodeKind k) {
    switch (k) {
    case NodeKind::Node:
        os << "Node";
        break;
    case NodeKind::TranslationUnit:
        os << "TranslationUnit";
        break;
    case NodeKind::Namespace:
        os << "Namespace";
        break;
    case NodeKind::BuiltinType:
        os << "BuiltinType";
        break;
    case NodeKind::PointerType:
        os << "PointerType";
        break;
    case NodeKind::RecordType:
        os << "RecordType";
        break;
    case NodeKind::EnumType:
        os << "EnumType";
        break;
    case NodeKind::FunctionProtoType:
        os << "FunctionProtoType";
        break;
    case NodeKind::Parm:
        os << "Parm";
        break;
    case NodeKind::Function:
        os << "Function";
        break;
    case NodeKind::Method:
        os << "Method";
        break;
    case NodeKind::Record:
        os << "Record";
        break;
    case NodeKind::Enum:
        os << "Enum";
        break;
    case NodeKind::ConstantArrayType:
        os << "ConstantArrayType";
        break;
    case NodeKind::Var:
        os << "Var";
        break;
    }
    return os;
}

/// Enumerates the different kinds of pointers and references
enum class PointerKind : uint32_t {
    Pointer,
    Reference,
    RValueReference,
};

/// Enumerates the different kinds of records.
/// OpaquePtr = opaque pointer to a C++ library type
/// OpaqueBytes = opaque bag of bytes containing a C++ library type
/// ValueType = C++ library type that is C-compatible (POD only)
enum class RecordKind : uint32_t { OpaquePtr = 0, OpaqueBytes, ValueType };

/// Typedef for representing a node in the AST. Signed int because we're
/// outputting to json
using NodeId = int32_t;

/// Abstract base struct for a node in the AST
struct Node {
    std::string qualified_name;
    NodeId id;
    NodeId context; //< parent context (e.g. record, namespce, TU)

    virtual NodeKind node_kind() const = 0;

    Node(std::string qualified_name, NodeId id, NodeId context)
        : qualified_name(qualified_name), id(id), context(context) {}

    virtual ~Node() {}

    virtual void write(std::ostream& os, int depth) const = 0;
};

using NodePtr = std::unique_ptr<Node>;

template <typename T> T* node_cast(Node* n) {
    assert(n->node_kind() == T::_kind && "incorrect node cast");
    if (n->node_kind() == T::_kind) {
        return reinterpret_cast<T*>(n);
    } else {
        return nullptr;
    }
}

template <typename T> const T* node_cast(const Node* n) {
    assert(n->node_kind() == T::_kind && "incorrect node cast");
    if (n->node_kind() == T::_kind) {
        return reinterpret_cast<T*>(n);
    } else {
        return nullptr;
    }
}

struct indent {
    int i;
};

std::ostream& operator<<(std::ostream& os, const indent& id) {
    for (int i = 0; i < id.i; ++i) {
        os << "    ";
    }
    return os;
}

/// Flat storage for nodes in the AST
std::vector<NodePtr> NODES;
/// Map for name-lookup of nodes (keys should match Node::qualified_name)
std::unordered_map<std::string, NodeId> NODE_MAP;
/// Root of the AST - will contain NodeTranslationUnits, which will themselves
/// contain the rest of the tree
std::vector<NodeId> ROOT;

/// Represents one translation unit (TU), i.e. one binding source file.
/// NodeTranslationUnit::qualified_name contains the filename
struct NodeTranslationUnit : public Node {
    /// Other nodes bound in this TU
    std::set<NodeId> children;
    /// Include statements from the binding file
    std::vector<std::string> source_includes;
    /// Include paths specified on the cppmm command line
    std::vector<std::string> project_includes;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    virtual void write(std::ostream& os, int depth) const override {
        os << "#include <" << source_includes[0] << ">\n";
        os << "#include <cppmm_bind.hpp>\n";
        os << "namespace cppmm_bind {\n\n";
        for (const NodeId child : children) {
            auto* node = NODES[child].get();
            node->write(os, depth);
            os << "\n\n";
        }
        os << "} // namespace cppmm_bind\n";
    }

    NodeTranslationUnit(std::string qualified_name, NodeId id, NodeId context,
                        std::vector<std::string> source_includes,
                        std::vector<std::string> project_includes)
        : Node(qualified_name, id, context), source_includes(source_includes),
          project_includes(project_includes) {}
};

/// Namespace node. Currently not used
struct NodeNamespace : public Node {
    std::string short_name;
    std::set<NodeId> children;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeNamespace(std::string qualified_name, NodeId id, NodeId context,
                  std::string short_name, std::set<NodeId> children)
        : Node(std::move(qualified_name), id, context),
          short_name(std::move(short_name)), children(std::move(children)) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth} << "namespace " << short_name << " {\n";
        for (const NodeId child : children) {
            auto* node = NODES[child].get();
            node->write(os, depth);
            os << "\n\n";
        }
        os << indent{depth} << "} // namespace " << short_name;
    }
};

/// Base struct represent a node that stores a type.
/// Types are references to the actual record and enum declarations that
/// describe those objects' structure. They are stored in the graph so that
/// types and the objects they reference can be processed out-of-order and then
/// the references fixed up later.
struct NodeType : public Node {
    std::string type_name;

    NodeType(std::string qualified_name, NodeId id, NodeId context,
             std::string type_name)
        : Node(qualified_name, id, context), type_name(type_name) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth};
        if (type_name == "_Bool") {
            os << "bool";
        } else {
            os << type_name;
        }
    }
};

struct NodeTemplateTypeParmType : public NodeType {
    NodeTemplateTypeParmType(std::string qualified_name, NodeId id,
                             NodeId context, std::string type_name)
        : NodeType(qualified_name, id, context, type_name) {}

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth} << type_name;
    }
};

/// A builtin, e.g. int, bool, char etc.
struct NodeBuiltinType : public NodeType {

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeBuiltinType(std::string qualified_name, NodeId id, NodeId context,
                    std::string type_name)
        : NodeType(qualified_name, id, context, type_name) {}
};

/// QType is the equivalent of clang's QualType. Currently just defines the
/// constness of the wrapped type node. These are stored on the AST nodes
/// anywhere a type is needed.
struct QType {
    /// Type we're constifying
    NodeId ty;
    bool is_const;

    void write(std::ostream& os, int depth) const {
        os << indent{depth};
        if (is_const) {
            os << "const ";
        }
        if (ty >= 0) {
            NODES.at(ty)->write(os, 0);
        } else {
            os << "UNKNOWN";
        }
    }

    bool operator==(const QType& rhs) const {
        return ty == rhs.ty && is_const == rhs.is_const;
    }

    bool operator!=(const QType& rhs) const { return !(*this == rhs); }
};

std::ostream& operator<<(std::ostream& os, const QType& q) {
    if (q.is_const) {
        os << "const ";
    }
    os << ((NodeType*)NODES.at(q.ty).get())->type_name;
    return os;
}

/// A pointer or reference type. The pointee is stored in pointee_type
struct NodePointerType : public NodeType {
    /// Type we're pointing to
    QType pointee_type;
    /// Is this a pointer, reference or r-value reference?
    PointerKind pointer_kind;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodePointerType(std::string qualified_name, NodeId id, NodeId context,
                    std::string type_name, PointerKind pointer_kind,
                    QType pointee_type)
        : NodeType(qualified_name, id, context, type_name),
          pointer_kind(pointer_kind), pointee_type(pointee_type) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth};
        pointee_type.write(os, 0);
        if (pointer_kind == PointerKind::Pointer) {
            os << "*";
        } else if (pointer_kind == PointerKind::Reference) {
            os << "&";
        } else if (pointer_kind == PointerKind::RValueReference) {
            os << "&&";
        }
    }
};

/// A C-style array, e.g. float[3]
struct NodeConstantArrayType : public NodeType {
    QType element_type;
    uint64_t size;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeConstantArrayType(std::string qualified_name, NodeId id, NodeId context,
                          std::string type_name, QType element_type,
                          uint64_t size)
        : NodeType(std::move(qualified_name), id, context,
                   std::move(type_name)),
          element_type(element_type), size(size) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth};
        element_type.write(os, 0);
        os << "[" << size << "]";
    }
};

/// An enum type reference
struct NodeEnumType : public NodeType {
    /// The enum declaration node, i.e. the actual type declaration. If the
    /// enum referred to hasn't been processed yet, then this will be -1 until
    /// such a time as the enum is processed
    NodeId enm;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeEnumType(std::string qualified_name, NodeId id, NodeId context,
                 std::string type_name, NodeId enm)
        : NodeType(qualified_name, id, context, type_name), enm(enm) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth} << type_name;
    }
};

/// A function prototype (a pointer to which can be passed as callbacks etc).
/// This sits in an awkward spot because there isn't a corresponding decl so all
/// the structure is packed onto the type node here
struct NodeFunctionProtoType : public NodeType {
    /// Return type of the function
    QType return_type;
    /// Function parameters
    std::vector<QType> params;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeFunctionProtoType(std::string qualified_name, NodeId id, NodeId context,
                          std::string type_name, QType return_type,
                          std::vector<QType> params)
        : NodeType(qualified_name, id, context, type_name),
          return_type(std::move(return_type)), params(std::move(params)) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth} << type_name;
    }
};

/// Param is essentially just a (name, type) pair forming a function parameter.
struct Param {
    /// parameter name
    std::string name;
    /// parameter type
    QType qty;
    /// index of the parameter in the function's parameter list
    int index;
    /// Any annnotation attributes on the parameter
    std::vector<std::string> attrs;

    void write(std::ostream& os, int depth) const {
        os << indent{depth};
        qty.write(os, 0);
        os << " " << name;
    }
};

std::ostream& operator<<(std::ostream& os, const Param& p) {
    return os << p.name << ": " << p.qty;
}

/// Base struct representing a node that has annotation attributes attached
struct NodeAttributeHolder : public Node {
    /// The annotation attributes
    std::vector<std::string> attrs;

    NodeAttributeHolder(std::string qualified_name, NodeId id, NodeId context,
                        std::vector<std::string> attrs)
        : Node(qualified_name, id, context), attrs(attrs) {}
};

struct NodeVar : public NodeAttributeHolder {
    std::string short_name;
    QType qtype;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeVar(std::string qualified_name, NodeId id, NodeId context,
            std::vector<std::string> attrs, std::string short_name, QType qtype)
        : NodeAttributeHolder(std::move(qualified_name), id, context,
                              std::move(attrs)),
          qtype(qtype), short_name(std::move(short_name)) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth};
        qtype.write(os, 0);
        os << " " << short_name;
    }
};

/// A function node
struct NodeFunction : public NodeAttributeHolder {
    /// What you think of as the function name without any qualifications
    std::string short_name;
    /// The function's return type
    QType return_type;
    /// The function's parameters
    std::vector<Param> params;
    /// Is this function declared in the binding? NOT USED
    bool in_binding = false;
    /// Is this function declared in the library? NOT USED
    bool in_library = false;
    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeFunction(std::string qualified_name, NodeId id, NodeId context,
                 std::vector<std::string> attrs, std::string short_name,
                 QType return_type, std::vector<Param> params)
        : NodeAttributeHolder(qualified_name, id, context, attrs),
          short_name(short_name), return_type(return_type), params(params) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth} << qualified_name;
    }
};

std::ostream& operator<<(std::ostream& os, const NodeFunction& f) {
    os << f.qualified_name << "(";
    for (const Param& p : f.params) {
        os << p << ", ";
    }
    os << ") -> " << f.return_type;
    return os;
}

/// A method on a class or struct.
struct NodeMethod : public NodeFunction {
    bool is_static = false;
    /// Is the method user-provided (i.e. !default)
    bool is_user_provided = false;
    bool is_const = false;
    bool is_virtual = false;
    bool is_overloaded_operator = false;
    bool is_copy_assignment_operator = false;
    bool is_move_assignment_operator = false;
    bool is_constructor = false;
    bool is_default_constructor = false;
    bool is_copy_constructor = false;
    bool is_move_constructor = false;
    /// Is the method a conversion decl, e.g. "operator bool()"
    bool is_conversion_decl = false;
    bool is_destructor = false;
    /// Is this method the result of a FunctionTemplateDecl specialization
    /// We use this to differentiate `foo(int)` from `foo<T>(T)` with `T=int`.
    bool is_specialization = false;
    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeMethod(std::string qualified_name, NodeId id, NodeId context,
               std::vector<std::string> attrs, std::string short_name,
               QType return_type, std::vector<Param> params, bool is_static)
        : NodeFunction(qualified_name, id, context, attrs, short_name,
                       return_type, params),
          is_static(is_static) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth};
        if (is_static) {
            os << "static ";
        }
        if (!is_constructor && !is_destructor) {
            os << "auto ";
        }
        os << short_name << "(";
        bool first = true;
        for (const auto& p : params) {
            if (!first) {
                os << ", ";
            }
            first = false;
            p.write(os, 0);
        }
        os << ")";
        if (is_const) {
            os << " const";
        }
        if (!is_constructor && !is_destructor) {
            os << " -> ";
            return_type.write(os, 0);
        }
        os << ";";
    }
};

std::ostream& operator<<(std::ostream& os, const NodeMethod& f) {
    if (f.is_static) {
        os << "static ";
    }
    if (f.is_virtual) {
        os << "virtual ";
    }
    os << f.qualified_name << "(";
    for (const Param& p : f.params) {
        os << p << ", ";
    }
    os << ") -> " << f.return_type;
    if (f.is_const) {
        os << " const";
    }
    os << "[[";
    if (f.is_user_provided) {
        os << "user_provided, ";
    }
    if (f.is_overloaded_operator) {
        os << "overloaded_operator, ";
    }
    if (f.is_copy_assignment_operator) {
        os << "copy_assignment_operator, ";
    }
    if (f.is_move_assignment_operator) {
        os << "move_assignment_operator, ";
    }
    if (f.is_constructor) {
        os << "constructor, ";
    }
    if (f.is_copy_constructor) {
        os << "copy_constructor, ";
    }
    if (f.is_move_constructor) {
        os << "move_constructor, ";
    }
    if (f.is_conversion_decl) {
        os << "conversion_decl, ";
    }
    if (f.is_destructor) {
        os << "destructor, ";
    }
    os << "]]";
    return os;
}

/// A field of a class or struct as a (name, type) pair
struct Field {
    std::string name;
    QType qtype;
};

/// A record is a class or struct declaration containing fields and methods
struct NodeRecord : public NodeAttributeHolder {
    std::vector<Field> fields;
    std::vector<NodeId> methods;
    /// The 'leaf' of the qualified name
    std::string short_name;
    /// The full path of namespaces leading to this record
    std::vector<NodeId> namespaces;
    /// Alias for the record, set by e.g.:
    /// using V3f = Imath::Vec3<float>;
    std::string alias;
    /// Does the class have any pure virtual functions?
    bool is_abstract;
    /// Any template parameters. If this is empty, this is not a template class
    std::vector<std::string> template_parameters;
    std::set<std::string> children;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeRecord(std::string qualified_name, NodeId id, NodeId context,
               std::vector<std::string> attrs, std::string short_name,
               std::vector<NodeId> namespaces,
               std::vector<std::string> template_parameters)
        : NodeAttributeHolder(qualified_name, id, context, attrs),
          short_name(std::move(short_name)), namespaces(std::move(namespaces)),
          template_parameters(std::move(template_parameters)) {}

    virtual void write(std::ostream& os, int depth) const override {
        if (template_parameters.size() > 0) {
            os << indent{depth} << "template <class ";
            os << ps::join(", class ", template_parameters);
            os << ">\n";
        }
        os << indent{depth} << "struct " << short_name << " {\n";

        for (const auto mid : methods) {
            NODES[mid]->write(os, depth + 1);
            os << "\n";
        }

        os << indent{depth} << "}; // struct " << short_name;
    }
};

/// A reference to a record (i.e. a class or struct)
struct NodeRecordType : public NodeType {
    /// The record declaration node, i.e. the actual type declaration. If the
    /// record referred to hasn't been processed yet, then this will be -1 until
    /// such a time as the record is processed
    NodeId record;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeRecordType(std::string qualified_name, NodeId id, NodeId context,
                   std::string type_name, NodeId record)
        : NodeType(qualified_name, id, context, type_name), record(record) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth};
        if (record != -1) {
            auto node_rec = node_cast<NodeRecord>(NODES.at(record).get());
            for (auto id : node_rec->namespaces) {
                auto node_ns = node_cast<NodeNamespace>(NODES.at(id).get());
                os << node_ns->short_name << "::";
            }
        }
        os << type_name;
    }
};

/// An enum declaration, just a list of (name, value) pairs of the variants
struct NodeEnum : public NodeAttributeHolder {
    /// C++ allows variant values larger than an int, while C only allows ints.
    /// Without knowing what the values are, it's impossible to say what type
    /// we'll need to store the values here. Most likely an int would be fine,
    /// but we can't rely on people not using crazy sentinel values, so we
    /// store it as a string and kick the can down the road to the generator
    std::vector<std::pair<std::string, std::string>> variants;
    /// Name of the enum without any qualifiers
    std::string short_name;
    /// Full namespace path
    std::vector<NodeId> namespaces;
    /// Size of the enum in bits
    uint32_t size;
    /// Alignment of the enum in bits
    uint32_t align;

    static NodeKind _kind;
    virtual NodeKind node_kind() const override { return _kind; }

    NodeEnum(std::string qualified_name, NodeId id, NodeId context,
             std::vector<std::string> attrs, std::string short_name,
             std::vector<NodeId> namespaces,
             std::vector<std::pair<std::string, std::string>> variants,
             uint32_t size, uint32_t align)
        : NodeAttributeHolder(qualified_name, id, context, attrs),
          short_name(std::move(short_name)), namespaces(std::move(namespaces)),
          variants(variants), size(size), align(align) {}

    virtual void write(std::ostream& os, int depth) const override {
        os << indent{depth} << qualified_name;
    }
};

NodeKind NodeNamespace::_kind = NodeKind::Namespace;
NodeKind NodeTranslationUnit::_kind = NodeKind::TranslationUnit;
NodeKind NodeBuiltinType::_kind = NodeKind::BuiltinType;
NodeKind NodePointerType::_kind = NodeKind::PointerType;
NodeKind NodeRecordType::_kind = NodeKind::RecordType;
NodeKind NodeEnumType::_kind = NodeKind::EnumType;
NodeKind NodeFunctionProtoType::_kind = NodeKind::FunctionProtoType;
NodeKind NodeFunction::_kind = NodeKind::Function;
NodeKind NodeMethod::_kind = NodeKind::Method;
NodeKind NodeRecord::_kind = NodeKind::Record;
NodeKind NodeEnum::_kind = NodeKind::Enum;
NodeKind NodeConstantArrayType::_kind = NodeKind::ConstantArrayType;
NodeKind NodeVar::_kind = NodeKind::Var;
NodeKind NodeTemplateTypeParmType::_kind = NodeKind::TemplateTypeParmType;

/// Strip the type kinds off the front of a type name in the given string
std::string strip_name_kinds(std::string s) {
    s = pystring::replace(s, "class ", "");
    s = pystring::replace(s, "struct ", "");
    s = pystring::replace(s, "enum ", "");
    s = pystring::replace(s, "union ", "");
    return s;
}

/// Get a nice, qualified name for the given record
std::string get_record_name(const CXXRecordDecl* crd) {
    // we have to do this dance to get the template parameters in the name,
    // otherwise they're omitted
    return strip_name_kinds(
        crd->getTypeForDecl()->getCanonicalTypeInternal().getAsString());
}

/// Find the node corresponding to the given TU filename, creating one if
/// none exists
NodeTranslationUnit* get_translation_unit(const std::string& filename) {
    auto it = NODE_MAP.find(filename);
    if (it != NODE_MAP.end()) {
        const auto& node = NODES.at(it->second);
        assert(node->node_kind() == NodeKind::TranslationUnit &&
               "node is wrong type");
        return (NodeTranslationUnit*)node.get();
    }

    NodeId id = NODES.size();
    auto node = std::make_unique<NodeTranslationUnit>(
        filename, id, 0, std::vector<std::string>{},
        std::vector<std::string>{});
    ROOT.push_back(id);
    auto* node_ptr = node.get();
    NODES.emplace_back(std::move(node));
    NODE_MAP[filename] = id;
    return node_ptr;
}

/// Get the full set of namespaces (including parent records) that lead to
/// a given decl. The decl passed here is expected to be the *parent* of the
/// decl we care about, as in `get_namespaces(target_decl->getParent())`
std::vector<NodeId> get_namespaces(NodeId child,
                                   const clang::DeclContext* parent,
                                   NodeTranslationUnit* node_tu) {
    std::vector<NodeId> result;

    NodeId id;
    SPDLOG_TRACE("Getting namespaces for {}", child);
    while (parent) {
        if (parent->isNamespace()) {
            const clang::NamespaceDecl* ns =
                static_cast<const clang::NamespaceDecl*>(parent);

            auto qualified_name = ns->getQualifiedNameAsString();
            auto short_name = ns->getNameAsString();

            SPDLOG_TRACE("Parent is namespace {}", qualified_name);

            // Add the id of this namespace to our list of namespaces, creating
            // a new NodeNamespace if it doesn't exist yet
            auto it = NODE_MAP.find(qualified_name);
            if (it == NODE_MAP.end()) {
                id = NODES.size();
                auto node = std::make_unique<NodeNamespace>(
                    qualified_name, id, 0, short_name, std::set<NodeId>{child});
                NODES.emplace_back(std::move(node));
                NODE_MAP[qualified_name] = id;

            } else {
                id = it->second;
            }

            SPDLOG_TRACE("Namespace id is {}", id);

            auto* node_ns = node_cast<NodeNamespace>(NODES[id].get());
            node_ns->children.insert(child);
            SPDLOG_TRACE("INserting {} in {}", child, id);
            result.push_back(id);

            parent = parent->getParent();
            child = id;
        } else if (parent->isRecord()) {
            // Parent is a Record type. We should have created the record
            // already by the time we get here...
            const clang::CXXRecordDecl* crd =
                static_cast<const clang::CXXRecordDecl*>(parent);

            auto record_name = get_record_name(crd);
            auto it = NODE_MAP.find(record_name);
            if (it == NODE_MAP.end()) {
                SPDLOG_CRITICAL(
                    "Could not find record {} when processing namespaces",
                    record_name);
            } else {
                id = it->second;
                result.push_back(id);
                auto* node_ns = node_cast<NodeNamespace>(NODES[id].get());
                node_ns->children.insert(child);
            }

            parent = parent->getParent();
            child = id;
        } else {
            break;
        }
    }

    node_tu->children.insert(id);

    std::reverse(result.begin(), result.end());
    return result;
}
// Get the canonical definition from a crd
const CXXRecordDecl* get_canonical_def_from_crd(const CXXRecordDecl* crd) {
    if ((crd = crd->getCanonicalDecl())) {
        return crd->getDefinition();
    }
    return nullptr;
}

// Get the templtaed canonical definition decl
const CXXRecordDecl* get_canonical_def_from_ctd(const ClassTemplateDecl* ctd) {
    if ((ctd = ctd->getCanonicalDecl())) {
        if (const auto* crd = ctd->getTemplatedDecl()) {
            return get_canonical_def_from_crd(crd);
        }
    }

    return nullptr;
}

std::vector<std::string> get_template_parameters(const ClassTemplateDecl* ctd) {
    const auto* tpl = ctd->getTemplateParameters();
    std::vector<std::string> result;

    if (tpl == nullptr) {
        SPDLOG_ERROR("Could not get template parameter list for {}",
                     ctd->getQualifiedNameAsString());
        return result;
    }

    for (const auto& nd : *tpl) {
        result.push_back(nd->getNameAsString());
    }

    return result;
}

QType process_qtype(
    const QualType& qt,
    const std::vector<std::vector<std::string>>& template_parameters);

/// Create a new NodeFunctionProtoType from the given FunctionProtoType and
/// return its id.
NodeId process_function_proto_type(const FunctionProtoType* fpt,
                                   std::string type_name,
                                   std::string type_node_name) {
    auto it = NODE_MAP.find(type_node_name);
    if (it != NODE_MAP.end()) {
        // already have an entry for this
        return it->second;
    } else {
        std::vector<std::vector<std::string>> template_parameters;
        QType return_type =
            process_qtype(fpt->getReturnType(), template_parameters);
        std::vector<QType> params;
        for (const QualType& pqt : fpt->param_types()) {
            params.push_back(process_qtype(pqt, template_parameters));
        }

        NodeId id = NODES.size();
        auto node_ptr = std::make_unique<NodeFunctionProtoType>(
            type_node_name, id, 0, std::move(type_name), std::move(return_type),
            std::move(params));
        NODES.emplace_back(std::move(node_ptr));
        NODE_MAP[type_node_name] = id;
        return id;
    }
}

/// Create a QType from the given QualType. Recursively processes the contained
/// types.
QType process_qtype(
    const QualType& qt,
    const std::vector<std::vector<std::string>>& template_parameters) {
    if (qt->isPointerType() || qt->isReferenceType()) {
        // first, figure out what kind of pointer we have
        auto pointer_kind = PointerKind::Pointer;
        if (qt->isRValueReferenceType()) {
            pointer_kind = PointerKind::RValueReference;
        } else if (qt->isReferenceType()) {
            pointer_kind = PointerKind::Reference;
        }

        // first check if we've got the pointer type already
        const std::string pointer_type_name =
            qt.getCanonicalType().getAsString();
        const std::string pointer_type_node_name = "TYPE:" + pointer_type_name;

        auto it = NODE_MAP.find(pointer_type_name);
        NodeId id;
        if (it == NODE_MAP.end()) {
            // need to create the pointer type, create the pointee type first
            QType pointee_qtype = process_qtype(
                qt->getPointeeType().getCanonicalType(), template_parameters);
            // now create the pointer type
            id = NODES.size();
            auto node_pointer_type = std::make_unique<NodePointerType>(
                pointer_type_node_name, id, 0, pointer_type_name, pointer_kind,
                pointee_qtype);
            NODES.emplace_back(std::move(node_pointer_type));
            NODE_MAP[pointer_type_name] = id;
        } else {
            // already done this type
            id = it->second;
        }

        return QType{id, qt.isConstQualified()};
    } else if (qt->isConstantArrayType()) {
        // e.g. float[3]
        const std::string type_name = qt.getCanonicalType().getAsString();
        const std::string type_node_name = "TYPE:" + type_name;
        auto it = NODE_MAP.find(type_node_name);
        NodeId id;
        if (it == NODE_MAP.end()) {
            const ConstantArrayType* cat =
                dyn_cast<ConstantArrayType>(qt.getTypePtr());
            QType element_type =
                process_qtype(cat->getElementType(), template_parameters);
            id = NODES.size();
            auto node_type = std::make_unique<NodeConstantArrayType>(
                type_node_name, id, 0, type_name, element_type,
                cat->getSize().getLimitedValue());
            NODES.emplace_back(std::move(node_type));
            NODE_MAP[type_node_name] = id;
        } else {
            id = it->second;
        }

        return QType{id, qt.isConstQualified()};
    } else if (qt->isTemplateTypeParmType()) {
        const auto* ttpt = qt->getAs<TemplateTypeParmType>();
        int depth = ttpt->getDepth();
        int index = ttpt->getIndex();
        assert(template_parameters.size() > depth &&
               "template parameters is not deep enough");
        assert(template_parameters[depth].size() > index &&
               "template parameters is not long enough");
        const std::string type_name = template_parameters[depth][index];
        const std::string type_node_name = "TYPE:" + type_name;

        auto it = NODE_MAP.find(type_node_name);
        NodeId id;
        if (it == NODE_MAP.end()) {
            id = NODES.size();
            auto tp =
                std::vector<std::vector<std::string>>{template_parameters};
            auto node_type = std::make_unique<NodeTemplateTypeParmType>(
                type_node_name, id, 0, type_name);
            NODES.emplace_back(std::move(node_type));
            NODE_MAP[type_node_name] = id;
        } else {
            id = it->second;
        }

        return QType{id, qt.isConstQualified()};
    } else {
        // regular type, let's get a nice name for it by removing the
        // class/struct/enum/union qualifier clang adds
        std::string type_name = strip_name_kinds(
            qt.getCanonicalType().getUnqualifiedType().getAsString());

        // We need to store type nodes for later access, since we might process
        // the corresponding record decl after processing this type node, and
        // will need to look it up later to set the appropriate id.
        // to get around the fact that the type and the record it refers to will
        // have the same name, we just prepend "TYPE:" to the type node name
        // here.
        // FIXME: we might want to stores types in a completely separate data
        // structure
        std::string type_node_name = "TYPE:" + type_name;

        // see if we've proessed this type already
        auto it = NODE_MAP.find(type_node_name);
        NodeId id;
        if (it == NODE_MAP.end()) {
            // haven't done this type yet we'll need to create a new node for it
            id = NODES.size();
            if (qt->isBuiltinType()) {
                // It's just a builtin. We store its name
                auto node_type = std::make_unique<NodeBuiltinType>(
                    type_node_name, id, 0, type_name);
                NODES.emplace_back(std::move(node_type));
                NODE_MAP[type_node_name] = id;
            } else if (qt->isRecordType()) {
                auto crd = qt->getAsCXXRecordDecl();
                assert(crd && "CRD from Type is null");
                crd = crd->getCanonicalDecl();
                assert(crd && "CRD canonical decl is null");

                // See if we've already processed a record matching this type
                // and get its id if we have. If not we'll store -1 until we
                // come back and process the decl later.
                const std::string record_name = crd->getQualifiedNameAsString();
                NodeId id_rec = -1;
                auto it_rec = NODE_MAP.find(record_name);
                if (it_rec != NODE_MAP.end()) {
                    id_rec = it_rec->second;
                }

                auto node_record_type = std::make_unique<NodeRecordType>(
                    type_node_name, id, 0, type_name, id_rec);
                NODES.emplace_back(std::move(node_record_type));
                NODE_MAP[type_node_name] = id;
            } else if (qt->isEnumeralType()) {
                const auto* et = qt->getAs<EnumType>();
                assert(et && "Could not get EnumType from Type");
                const auto* ed = et->getDecl();
                assert(ed && "Could not get EnumDecl from EnumType");
                ed = ed->getCanonicalDecl();
                assert(ed && "Could not get canonical EnumDecl from EnumType");

                // see if we've already processed an enum matching this type
                // and get its id if we have. If not we'll store -1 until we
                // come back and process the decl later.
                const std::string enum_qual_name =
                    ed->getQualifiedNameAsString();
                NodeId id_enum = -1;
                auto it_enum = NODE_MAP.find(enum_qual_name);
                if (it_enum != NODE_MAP.end()) {
                    id_enum = it_enum->second;
                }

                auto node_enum_type = std::make_unique<NodeEnumType>(
                    type_node_name, id, 0, type_name, id_enum);
                NODES.emplace_back(std::move(node_enum_type));
                NODE_MAP[type_node_name] = id;
            } else if (qt->isFunctionProtoType()) {
                const auto* fpt = qt->getAs<FunctionProtoType>();
                assert(fpt && "Could not get FunctionProtoType from QualType");

                id =
                    process_function_proto_type(fpt, type_name, type_node_name);
            } else if (qt->isDependentType()) {
                const auto* crd = qt->getAsCXXRecordDecl();
                if (crd) {
                    // See if we've already processed a record matching this
                    // type and get its id if we have. If not we'll store -1
                    // until we come back and process the decl later.
                    const std::string record_name =
                        crd->getQualifiedNameAsString();
                    NodeId id_rec = -1;
                    auto it_rec = NODE_MAP.find(record_name);
                    if (it_rec != NODE_MAP.end()) {
                        id_rec = it_rec->second;
                    }

                    auto node_record_type = std::make_unique<NodeRecordType>(
                        type_node_name, id, 0, type_name, id_rec);
                    NODES.emplace_back(std::move(node_record_type));
                    NODE_MAP[type_node_name] = id;
                } else {
                    SPDLOG_TRACE("IS DEPENDENT TYPE");
                    id = NodeId(-1);
                }
            } else {
                SPDLOG_WARN("Unhandled type {}", type_node_name);
                qt->dump();
                id = NodeId(-1);
            }
        } else {
            id = it->second;
        }

        return QType{id, qt.isConstQualified()};
    }
}

void process_function_parameters(
    const FunctionDecl* fd, QType& return_qtype, std::vector<Param>& params,
    const std::vector<std::vector<std::string>>& template_parameters) {
    SPDLOG_TRACE("    -> {}", fd->getReturnType().getAsString());
    return_qtype = process_qtype(fd->getReturnType(), template_parameters);

    for (const ParmVarDecl* pvd : fd->parameters()) {
        int index = pvd->getFunctionScopeIndex();
        SPDLOG_TRACE("        {}: {}", pvd->getQualifiedNameAsString(),
                     pvd->getType().getCanonicalType().getAsString());
        QType qtype = process_qtype(pvd->getType(), template_parameters);

        params.emplace_back(Param{pvd->getNameAsString(), qtype, index, {}});

        if (const auto* vtd = pvd->getDescribedVarTemplate()) {
            SPDLOG_TRACE("            GOT VTD");
        }
        if (const auto* td = pvd->getDescribedTemplate()) {
            SPDLOG_TRACE("            GOT TD");
        }
    }
}

/// Create a new node for the given method decl and return it
NodePtr process_method_decl(
    const CXXMethodDecl* cmd, std::vector<std::string> attrs,
    const std::vector<std::vector<std::string>>& template_parameters,
    bool is_specialization = false) {
    const std::string method_name = cmd->getQualifiedNameAsString();
    const std::string method_short_name = cmd->getNameAsString();

    QType return_qtype;
    std::vector<Param> params;
    process_function_parameters(cmd, return_qtype, params, template_parameters);

    auto node_function = std::make_unique<NodeMethod>(
        method_name, 0, 0, std::move(attrs), method_short_name, return_qtype,
        std::move(params), cmd->isStatic());

    NodeMethod* m = (NodeMethod*)node_function.get();
    m->is_user_provided = cmd->isUserProvided();
    m->is_const = cmd->isConst();
    m->is_virtual = cmd->isVirtual();
    m->is_overloaded_operator = cmd->isOverloadedOperator();
    m->is_copy_assignment_operator = cmd->isCopyAssignmentOperator();
    m->is_move_assignment_operator = cmd->isMoveAssignmentOperator();

    if (const auto* ccd = dyn_cast<CXXConstructorDecl>(cmd)) {
        m->is_constructor = true;
        m->is_copy_constructor = ccd->isCopyConstructor();
        m->is_move_constructor = ccd->isMoveConstructor();
    } else if (const auto* cdd = dyn_cast<CXXDestructorDecl>(cmd)) {
        m->is_destructor = true;
    } else if (const auto* ccd = dyn_cast<CXXConversionDecl>(cmd)) {
        m->is_conversion_decl = true;
    }

    m->is_specialization = is_specialization;

    SPDLOG_DEBUG("Processed method {}", m->qualified_name);

    return node_function;
}

std::vector<NodeId>
process_methods(const CXXRecordDecl* crd,
                std::vector<std::vector<std::string>> template_parameters) {
    std::vector<NodePtr> result;
    SPDLOG_TRACE("process_methods({})", get_record_name(crd));

    // FIXME: need to replace existing methods from the base class
    // for overrides
    for (const Decl* d : crd->decls()) {
        // we want to ignore anything that's not public for obvious reasons
        // since we're using this function for getting methods both from the
        // library type and the binding type, this does mean we need to add a
        // "public" specifier to the binding type, but eh...
        if (d->getAccess() != AS_public) {
            continue;
        }

        // A FunctionTemplateDecl represents methods that are dependent on
        // their own template parameters (aside from the Record template
        // parameter list).
        if (const FunctionTemplateDecl* ftd =
                dyn_cast<FunctionTemplateDecl>(d)) {
            for (const FunctionDecl* fd : ftd->specializations()) {
                std::vector<std::string> attrs{};
                if (const auto* cmd = dyn_cast<CXXMethodDecl>(fd)) {
                    auto node_function = process_method_decl(
                        cmd, attrs, template_parameters, true);
                    // add_method_to_list(std::move(node_function), result);
                    result.emplace_back(std::move(node_function));
                } else {
                    // shouldn't get here
                    assert(false && "method spec couldn't be converted to CMD");
                    const std::string function_name =
                        ftd->getQualifiedNameAsString();
                    const std::string method_short_name = fd->getNameAsString();
                    QType return_qtype;
                    std::vector<Param> params;
                    process_function_parameters(fd, return_qtype, params,
                                                template_parameters);

                    auto node_function = std::make_unique<NodeMethod>(
                        function_name, -1, -1, std::move(attrs),
                        method_short_name, return_qtype, std::move(params),
                        fd->isStatic());
                    // add_method_to_list(std::move(node_function), result);
                    result.emplace_back(std::move(node_function));
                }
            }
        } else if (const auto* cmd = dyn_cast<CXXMethodDecl>(d)) {
            // just a regular boring old method
            std::vector<std::string> attrs{};
            auto node_function =
                process_method_decl(cmd, attrs, template_parameters);
            // add_method_to_list(std::move(node_function), result);
            result.emplace_back(std::move(node_function));
        }
    }

    std::vector<NodeId> method_ids;
    method_ids.reserve(result.size());

    for (auto&& m : result) {
        NodeId id = NODES.size();
        NODE_MAP[m->qualified_name] = id;
        NODES.emplace_back(std::move(m));
        method_ids.push_back(id);
    }

    return method_ids;
}

void process_crd(const CXXRecordDecl* crd,
                 std::vector<std::string> template_parameters) {
    ASTContext& ctx = crd->getASTContext();
    SourceManager& sm = ctx.getSourceManager();
    const auto& loc = crd->getLocation();
    std::string filename = sm.getFilename(loc).str();

    auto qualified_name = crd->getQualifiedNameAsString();
    auto short_name = crd->getNameAsString();
    SPDLOG_DEBUG("process {} at {}:{}", qualified_name, filename,
                 sm.getExpansionLineNumber(loc));

    auto* node_tu = get_translation_unit(filename);
    node_tu->source_includes.push_back(filename);

    auto it = NODE_MAP.find(qualified_name);
    if (it == NODE_MAP.end()) {
        SPDLOG_TRACE("Not found, adding");
        NodeId id = NODES.size();
        auto node_record = std::make_unique<NodeRecord>(
            qualified_name, id, 0, std::vector<std::string>{},
            std::move(short_name), std::vector<NodeId>{}, template_parameters);

        NODES.emplace_back(std::move(node_record));
        SPDLOG_TRACE("inserting {} : {}", qualified_name, id);
        NODE_MAP[qualified_name] = id;

        auto namespaces = get_namespaces(id, crd->getParent(), node_tu);
        auto* node_rec = node_cast<NodeRecord>(NODES[id].get());
        node_rec->namespaces = std::move(namespaces);

        node_rec->methods = process_methods(
            crd, std::vector<std::vector<std::string>>{template_parameters});

        // fix up any record type references
        auto it_record_type = NODE_MAP.find("TYPE:" + qualified_name);
        if (it_record_type != NODE_MAP.end()) {
            auto* node_record_type =
                (NodeRecordType*)NODES.at(it_record_type->second).get();
            node_record_type->record = id;
        }
    }
}

void handle_ctd(const ClassTemplateDecl* ctd) {
    ASTContext& ctx = ctd->getASTContext();
    SourceManager& sm = ctx.getSourceManager();
    const auto& loc = ctd->getLocation();
    std::string filename = sm.getFilename(loc).str();

    ctd = ctd->getCanonicalDecl();

    std::string qualified_name = ctd->getQualifiedNameAsString();

    // SPDLOG_TRACE("Got CTD {} at {}:{}", qualified_name, filename,
    //              sm.getExpansionLineNumber(loc));

    auto template_parameters = get_template_parameters(ctd);
    if (template_parameters.empty()) {
        SPDLOG_WARN("Class template {} had no template parameters",
                    qualified_name);
    }

    for (const auto& t : template_parameters) {
        SPDLOG_DEBUG("    {}", t);
    }

    const auto* crd = get_canonical_def_from_ctd(ctd);
    if (crd == nullptr) {
        SPDLOG_ERROR("Could not get CRD from CTD {}", qualified_name);
        return;
    }

    process_crd(crd, std::move(template_parameters));
}

} // namespace cppmm

class GenBindingCallback
    : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    virtual void
    run(const clang::ast_matchers::MatchFinder::MatchResult& result);
};

std::string CURRENT_FILENAME;
void GenBindingCallback::run(const MatchFinder::MatchResult& result) {
    if (const CXXRecordDecl* crd =
            result.Nodes.getNodeAs<CXXRecordDecl>("recordDecl")) {
        ASTContext& ctx = crd->getASTContext();
        SourceManager& sm = ctx.getSourceManager();
        const auto& loc = crd->getLocation();
        std::string filename = sm.getFilename(loc).str();

        if (filename == CURRENT_FILENAME) {
            crd = crd->getCanonicalDecl();
            // SPDLOG_TRACE("Got CRD {} as {}:{}",
            // crd->getQualifiedNameAsString(),
            //              filename, sm.getExpansionLineNumber(loc));

            if (const auto* ctd = crd->getDescribedClassTemplate()) {
                // this represents a specialization, ignore it
                // SPDLOG_TRACE("    is a spec of {}",
                //              ctd->getQualifiedNameAsString());
            }
        }

    } else if (const TypeAliasDecl* tdecl =
                   result.Nodes.getNodeAs<TypeAliasDecl>("typeAliasDecl")) {
        if (const auto* crd =
                tdecl->getUnderlyingType()->getAsCXXRecordDecl()) {
            SPDLOG_INFO("GOT CXXRECORDTTYPE {} from TAD {}",
                        crd->getQualifiedNameAsString(),
                        tdecl->getNameAsString());
            // handle_typealias_decl(tdecl, crd);
        }
    } else if (const FunctionDecl* function =
                   result.Nodes.getNodeAs<FunctionDecl>("functionDecl")) {
        // handle_binding_function(function);
    } else if (const EnumDecl* enum_decl =
                   result.Nodes.getNodeAs<EnumDecl>("enumDecl")) {
        // handle_binding_enum(enum_decl);
    } else if (const VarDecl* var_decl =
                   result.Nodes.getNodeAs<VarDecl>("varDecl")) {
        // handle_binding_var(var_decl);
    } else if (const ClassTemplateDecl* ctd =
                   result.Nodes.getNodeAs<ClassTemplateDecl>(
                       "classTemplateDecl")) {
        ctd = ctd->getCanonicalDecl();
        ASTContext& ctx = ctd->getASTContext();
        SourceManager& sm = ctx.getSourceManager();
        const auto& loc = ctd->getLocation();
        std::string filename = sm.getFilename(loc).str();
        if (filename == CURRENT_FILENAME) {
            cppmm::handle_ctd(ctd);
        }
    }
}

class GenBindingConsumer : public clang::ASTConsumer {
    clang::ast_matchers::MatchFinder _match_finder;
    GenBindingCallback _handler;
    std::string filename;

public:
    explicit GenBindingConsumer(clang::ASTContext* context);
    virtual void HandleTranslationUnit(clang::ASTContext& context);
};

class GenBindingAction : public clang::ASTFrontendAction {
    std::string filename;

public:
    GenBindingAction() {}

    virtual std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance& compiler,
                      llvm::StringRef in_file) {
        return std::unique_ptr<clang::ASTConsumer>(
            new GenBindingConsumer(&compiler.getASTContext()));
    }
};

GenBindingConsumer::GenBindingConsumer(ASTContext* context) {
    // match all record declrations in the cppmm_bind namespace
    DeclarationMatcher record_decl_matcher =
        cxxRecordDecl(hasAncestor(namespaceDecl(hasName("Imath_2_5"))),
                      unless(isImplicit()))
            .bind("recordDecl");
    _match_finder.addMatcher(record_decl_matcher, &_handler);

    DeclarationMatcher ctd_matcher =
        classTemplateDecl(hasAncestor(namespaceDecl(hasName("Imath_2_5"))),
                          unless(isImplicit()))
            .bind("classTemplateDecl");
    _match_finder.addMatcher(ctd_matcher, &_handler);

    // // match all typedef declrations in the cppmm_bind namespace
    // DeclarationMatcher typedef_decl_matcher =
    //     typeAliasDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
    //                   unless(hasAncestor(recordDecl())),
    //                   unless(isImplicit()))
    //         .bind("typeAliasDecl");
    // _match_finder.addMatcher(typedef_decl_matcher, &_handler);

    // // match all function declarations
    // DeclarationMatcher function_decl_matcher =
    //     functionDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
    //                  unless(hasAncestor(recordDecl())))
    //         .bind("functionDecl");
    // _match_finder.addMatcher(function_decl_matcher, &_handler);

    // // match all enum declrations in the cppmm_bind namespace
    // DeclarationMatcher enum_decl_matcher =
    //     enumDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
    //              unless(isImplicit()))
    //         .bind("enumDecl");
    // _match_finder.addMatcher(enum_decl_matcher, &_handler);

    // // match all variable declrations in the cppmm_bind namespace
    // DeclarationMatcher var_decl_matcher =
    //     varDecl(hasAncestor(namespaceDecl(hasName("cppmm_bind"))),
    //             unless(anyOf(isImplicit(), parmVarDecl())))
    //         .bind("varDecl");
    // _match_finder.addMatcher(var_decl_matcher, &_handler);
}

/// Run the binding AST matcher, then run secondary matchers to find functions
/// and enums we're interested in from the bindings (stored in the first pass)
void GenBindingConsumer::HandleTranslationUnit(ASTContext& context) {
    _match_finder.matchAST(context);

    /*
    for (const auto& fn : binding_functions) {
        SPDLOG_DEBUG("    {}", fn.first);
    }

    // add a matcher for each function we found in the binding
    for (const auto& kv : binding_functions) {
        for (const auto& fn : kv.second) {
            SPDLOG_DEBUG("Adding matcher for function {}", fn.short_name);
            DeclarationMatcher function_decl_matcher =
                functionDecl(
                    hasName(fn.short_name),
                    unless(hasAncestor(namespaceDecl(hasName("cppmm_bind")))),
                    unless(hasAncestor(recordDecl())))
                    .bind("libraryFunctionDecl");
            _library_finder.addMatcher(function_decl_matcher,
                                       &_library_handler);
        }
    }

    // and a matcher for each enum
    for (const auto& kv : binding_enums) {
        SPDLOG_DEBUG("Adding matcher for enum {}", kv.first);
        DeclarationMatcher enum_decl_matcher =
            enumDecl(hasName(kv.second.short_name),
                     unless(hasAncestor(namespaceDecl(hasName("cppmm_bind")))),
                     unless(hasAncestor(recordDecl())))
                .bind("libraryEnumDecl");
        _library_finder.addMatcher(enum_decl_matcher, &_library_handler);
    }

    // and a matcher for each var
    for (const auto& kv : binding_vars) {
        SPDLOG_DEBUG("Adding matcher for var {}", kv.first);
        DeclarationMatcher var_decl_matcher =
            varDecl(hasName(kv.second.short_name),
                    unless(hasAncestor(namespaceDecl(hasName("cppmm_bind")))))
                .bind("libraryVarDecl");
        _library_finder.addMatcher(var_decl_matcher, &_library_handler);
    }

    _library_finder.matchAST(context);
    */
}

std::vector<std::string> parse_project_includes(int argc, const char** argv) {
    std::vector<std::string> result;
    for (int i = 0; i < argc; ++i) {
        std::string a(argv[i]);
        if (a.find("-I") == 0) {
            result.push_back(a.substr(2, std::string::npos));
        }
    }
    return result;
}

// https://gist.github.com/rodamber/2558e25d4d8f6b9f2ffdf7bd49471340
// FIXME: Handle multiple uppers by inserting an underscore before the last
// upper. So IMFVersion becomes imf_version not imfversion
std::string to_snake_case(std::string camel_case) {
    std::string str(1, tolower(camel_case[0]));

    // First place underscores between contiguous lower and upper case letters.
    // For example, `_LowerCamelCase` becomes `_Lower_Camel_Case`.
    for (auto it = camel_case.begin() + 1; it != camel_case.end(); ++it) {
        if (isupper(*it) && *(it - 1) != '_' && islower(*(it - 1))) {
            str += "_";
        }
        str += *it;
    }

    // Then convert it to lower case.
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);

    return str;
}

//
// list of includes for each input source file
// this global is read in process_bindings.cpp
std::unordered_map<std::string, std::vector<std::string>> source_includes;
std::vector<std::string> project_includes;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory CppmmCategory("cppmm options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

static cl::opt<bool> opt_warn_unbound("u", cl::desc("Warn on unbound methods"));

static cl::opt<int> opt_verbosity(
    "v", cl::desc("Verbosity. 0=errors, 1=warnings, 2=info, 3=debug, 4=trace"));

static cl::opt<std::string> opt_output_directory(
    "o",
    cl::desc(
        "Directory under which output project directories will be written"));
static cl::list<std::string>
    opt_rename_namespace("n", cl::desc("Rename namespace <to>=<from>"));
static cl::opt<std::string> opt_rust_sys_directory(
    "rust-sys",
    cl::desc("Directory under which rust-sys project will be written"));

static cl::list<std::string>
    opt_includes("i", cl::desc("Extra includes for the project"));
static cl::list<std::string>
    opt_libraries("l", cl::desc("Libraries to link against"));

int main(int argc_, const char** argv_) {
    // set up logging
    auto _console = spdlog::stdout_color_mt("console");
    std::string cwd = fs::current_path();

    // FIXME: there's got to be a more sensible way of doing this but I can't
    // figure it out...
    int argc = argc_ + 2;
    const char** argv = new const char*[argc + 1];
    int i;
    bool has_ddash = false;
    for (i = 0; i < argc_; ++i) {
        argv[i] = argv_[i];
        if (!strcmp(argv[i], "--")) {
            has_ddash = true;
        }
    }

    // get the path to the binary, assuming that the resources folder will be
    // stored alongside it
    // FIXME: this method will work only on linux...
    auto exe_path = cwd / fs::path(argv[0]);
    if (fs::is_symlink(exe_path)) {
        exe_path = fs::read_symlink(exe_path);
    }
    if (exe_path.empty()) {
        SPDLOG_CRITICAL("Could not get exe path");
        return -1;
    }

    std::string respath1 = (exe_path.parent_path() / "resources").string();
    if (!has_ddash) {
        argv[i++] = "--";
        argc++;
    }
    argv[i++] = "-isystem";
    argv[i++] = respath1.c_str();

    project_includes = parse_project_includes(argc, argv);

    CommonOptionsParser OptionsParser(argc, argv, CppmmCategory);

    switch (opt_verbosity) {
    case 0:
        spdlog::set_level(spdlog::level::err);
        break;
    case 1:
        spdlog::set_level(spdlog::level::warn);
        break;
    case 2:
        spdlog::set_level(spdlog::level::info);
        break;
    case 3:
        spdlog::set_level(spdlog::level::debug);
        break;
    case 4:
        spdlog::set_level(spdlog::level::trace);
        break;
    default:
        spdlog::set_level(spdlog::level::warn);
        break;
    }
    spdlog::set_pattern("%20s:%4# %^[%5l]%$ %v");

    ArrayRef<std::string> src_path = OptionsParser.getSourcePathList();

    if (src_path.size() != 1) {
        SPDLOG_CRITICAL("Expected 1 header file to parse, got {}",
                        src_path.size());
        return 1;
    }

    if (!fs::is_regular_file(src_path[0])) {
        SPDLOG_CRITICAL("Header path \"{}\" is not a regular file",
                        src_path[0]);
        return 1;
    }

    std::vector<std::string> header_paths;
    std::vector<std::string> vtu;
    std::vector<std::string> vtu_paths;

    auto header_path = ps::os::path::abspath(src_path[0], cwd);
    header_paths.push_back(header_path);
    SPDLOG_DEBUG("Found header file {}", header_path);
    vtu.push_back(fmt::format("#include \"{}\"", header_path));
    vtu_paths.push_back(
        fmt::format("/tmp/{}.cpp", fs::path(header_path).stem().string()));

    auto& compdb = OptionsParser.getCompilations();

    std::string output_dir = cwd;
    if (opt_output_directory != "") {
        output_dir = opt_output_directory;
    }

    if (!fs::exists(output_dir) && !fs::create_directories(output_dir)) {
        SPDLOG_ERROR("Could not create output directory '{}'", output_dir);
        return -2;
    }

    for (int i = 0; i < vtu.size(); ++i) {
        SPDLOG_INFO("Processing {}", header_paths[i]);
        CURRENT_FILENAME = header_paths[i];
        ClangTool Tool(compdb, ArrayRef<std::string>(vtu_paths[i]));
        Tool.mapVirtualFile(vtu_paths[i], vtu[i]);

        auto process_binding_action =
            newFrontendActionFactory<GenBindingAction>();
        int result = Tool.run(process_binding_action.get());
    }

    using namespace cppmm;
    SPDLOG_DEBUG("output path is {}", output_dir);
    for (const NodeId id : ROOT) {
        auto* node_tu = node_cast<NodeTranslationUnit>(NODES[id].get());
        std::cout << "/// File:  " << node_tu->qualified_name << "\n\n";
        std::string relative_header = node_tu->qualified_name;
        // generate relative header path by matching against provided include
        // paths and stripping any match from the front.
        node_tu->source_includes.push_back(relative_header);
        for (const auto& p : project_includes) {
            if (ps::find(node_tu->qualified_name, p) == 0) {
                // we have a match
                relative_header =
                    ps::lstrip(ps::replace(relative_header, p, ""), "/");
                node_tu->source_includes[0] = relative_header;
                break;
            }
        }

        // generate output filename by snake_casing the header filename
        auto filename = to_snake_case(fs::path(node_tu->qualified_name)
                                          .filename()
                                          .replace_extension(".cpp")
                                          .string());
        auto output_path = output_dir / fs::path(filename);
        std::ofstream of;
        of.open(output_path);
        // write output file
        SPDLOG_INFO("Writing {}", output_path.string());
        node_tu->write(of, 0);
        of.close();
    }

    // return result;
}
