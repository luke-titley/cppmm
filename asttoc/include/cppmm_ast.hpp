//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cppmm {

struct Type;

using Id = uint64_t;

//------------------------------------------------------------------------------
// NodeKind
//------------------------------------------------------------------------------
enum class NodeKind : uint32_t {
    Node = 0,
    TranslationUnit,
    Namespace,
    ArrayType,
    BuiltinType,
    PointerType,
    RecordType,
    Parm,
    Function,
    FunctionCallExpr,
    MethodCallExpr,
    VarRefExpr,
    DerefExpr,
    CastExpr,
    Method,
    Record,
    Sentinal, // A sentinal entry to keep track of how many there are
};

//------------------------------------------------------------------------------
// PointerKind
//------------------------------------------------------------------------------
enum class PointerKind : uint32_t {
    Pointer,
    Reference,
};

using NodeId = uint64_t;

//------------------------------------------------------------------------------
// Node
//------------------------------------------------------------------------------
struct Node {
    std::string name;
    NodeId id;
    NodeKind kind;

    Node(std::string name, NodeId id, NodeKind kind)
        : name(name)
        , id(id)
        , kind(kind) {}

    virtual ~Node(){}
};
// Shared so the node can be stored in a tree and also in mapping.
using NodePtr = std::shared_ptr<Node>;

//------------------------------------------------------------------------------
// TranslationUnit
//------------------------------------------------------------------------------
struct TranslationUnit {
    std::string filename;
    std::vector<NodePtr> decls;

    using Self = TranslationUnit;

    TranslationUnit(std::string filename)
        : filename(filename)
    {}

    TranslationUnit(Self && rhs)
        : filename(std::move(rhs.filename))
        , decls(std::move(rhs.decls))
    {}

    void operator==(Self && rhs) {
        filename = std::move(rhs.filename);
        decls = std::move(rhs.decls);
    }
};

//------------------------------------------------------------------------------
// NodeNamespace
//------------------------------------------------------------------------------
struct NodeNamespace : public Node {};

//------------------------------------------------------------------------------
// NodeType
//------------------------------------------------------------------------------
struct NodeType : public Node {
    bool const_;
    std::string type_name;

    NodeType(std::string qualified_name, NodeId id,
             NodeKind node_kind, std::string type_name, bool const_)
        : Node(qualified_name, id, node_kind)
        , const_(const_)
        , type_name(type_name) {}
};
using NodeTypePtr = std::shared_ptr<NodeType>;

//------------------------------------------------------------------------------
// NodeBuiltinType
//------------------------------------------------------------------------------
struct NodeBuiltinType : public NodeType {

    NodeBuiltinType(std::string qualified_name, NodeId id,
                    std::string type_name, bool const_)
        : NodeType(qualified_name, id, NodeKind::BuiltinType,
                   type_name, const_)
    {}
};

//------------------------------------------------------------------------------
// NodePointerType
//------------------------------------------------------------------------------
/// pointer or reference type - check type_kind
struct NodePointerType : public NodeType {
    NodeTypePtr pointee_type;
    PointerKind pointer_kind;
    NodePointerType(std::string qualified_name, NodeId id,
                    std::string type_name, PointerKind pointer_kind,
                    NodeTypePtr && pointee_type, bool const_)
        : NodeType(qualified_name, id, NodeKind::PointerType, type_name, const_)
        , pointer_kind(pointer_kind), pointee_type(std::move(pointee_type)) {}
};

//------------------------------------------------------------------------------
// NodeRecordType
//------------------------------------------------------------------------------
struct NodeRecordType : public NodeType {
    NodeId record;
    NodeRecordType(std::string qualified_name, NodeId id,
                   std::string type_name, NodeId record, bool const_)
        : NodeType(qualified_name, id, NodeKind::RecordType, type_name, const_),
          record(record) {}
};

//------------------------------------------------------------------------------
// NodeArrayType
//------------------------------------------------------------------------------
// TODO LT: I added this one
struct NodeArrayType : public NodeType {
    uint64_t size;
    NodeTypePtr element_type;

    NodeArrayType(std::string qualified_name, NodeId id, std::string type_name,
                  NodeTypePtr && element_type, uint64_t size, bool const_)
        : NodeType(qualified_name, id, NodeKind::ArrayType, type_name, const_)
        , size(size)
        , element_type(std::move(element_type)) {}
};

//------------------------------------------------------------------------------
// Param
//------------------------------------------------------------------------------
struct Param {
    std::string name;
    NodeTypePtr type;
    int index;

    Param(std::string && name, NodeTypePtr && type, int index)
        : name(name)
        , type(std::move(type))
        , index(index)
    {}

    Param(Param && rhs)
        : name(std::move(rhs.name))
        , type(std::move(rhs.type))
        , index(rhs.index)
    {}

    Param(const Param & rhs)
        : name(rhs.name)
        , type(rhs.type)
        , index(rhs.index)
    {}
};

//------------------------------------------------------------------------------
// NodeAttributeHolder
//------------------------------------------------------------------------------
struct NodeAttributeHolder : public Node {
    std::vector<std::string> attrs;

    NodeAttributeHolder(std::string name, NodeId id,
                        NodeKind node_kind, std::vector<std::string> attrs)
        : Node(name, id, node_kind), attrs(attrs) {}
};

//------------------------------------------------------------------------------
// NodeExpr
//------------------------------------------------------------------------------
struct NodeExpr : public Node { // TODO LT: Added by luke

    NodeExpr(NodeKind kind)
        : Node("", 0, kind)
    {}
};
using NodeExprPtr = std::shared_ptr<NodeExpr>;

//------------------------------------------------------------------------------
// NodeFunctionCallExpr
//------------------------------------------------------------------------------
struct NodeFunctionCallExpr : public NodeExpr { // TODO LT: Added by luke, like CallExpr

    NodeFunctionCallExpr(NodeKind kind = NodeKind::FunctionCallExpr)
        : NodeExpr(kind)
    {}
};

//------------------------------------------------------------------------------
// NodeMethodCallExpr
//------------------------------------------------------------------------------
struct NodeMethodCallExpr : public NodeFunctionCallExpr { // TODO LT: Added by luke, like clang MemberCallExpr

    NodeMethodCallExpr()
        : NodeFunctionCallExpr(NodeKind::MethodCallExpr)
    {}
};

//------------------------------------------------------------------------------
// NodeVarRefExpr
//------------------------------------------------------------------------------
struct NodeVarRefExpr : public NodeExpr { // TODO LT: Added by luke, like clang DeclRef
    std::string var_name;

    NodeVarRefExpr(std::string var_name)
        : NodeExpr(NodeKind::VarRefExpr)
        , var_name(var_name)
    {}
};

//------------------------------------------------------------------------------
// NodeDerefExpr
//------------------------------------------------------------------------------
struct NodeDerefExpr : public NodeExpr { // TODO LT: Added by luke
    NodeExprPtr inner;

    NodeDerefExpr(NodeExprPtr && inner)
        : NodeExpr(NodeKind::DerefExpr)
        , inner(std::move(inner))
    {}
};

//------------------------------------------------------------------------------
// NodeCastExpr
//------------------------------------------------------------------------------
struct NodeCastExpr : public NodeExpr { // TODO LT: Added by luke
    NodeExprPtr inner;
    NodeTypePtr type;

    NodeCastExpr(NodeExprPtr && inner, NodeTypePtr && type)
        : NodeExpr(NodeKind::CastExpr)
        , inner(std::move(inner))
        , type(std::move(type))
    {}
};

//------------------------------------------------------------------------------
// NodeFunction
//------------------------------------------------------------------------------
struct NodeFunction : public NodeAttributeHolder {
    std::string short_name;
    NodeTypePtr return_type;
    std::vector<Param> params;
    bool in_binding = false;
    bool in_library = false;
    NodeExprPtr body;

    NodeFunction(std::string qualified_name, NodeId id,
                 std::vector<std::string> attrs, std::string short_name,
                 NodeTypePtr && return_type, std::vector<Param> && params)
        : NodeAttributeHolder(qualified_name, id, NodeKind::Function,
                              attrs),
          short_name(short_name), return_type(std::move(return_type)),
          params(std::move(params)) {}
};

//------------------------------------------------------------------------------
// NodeMethod
//------------------------------------------------------------------------------
struct NodeMethod : public NodeFunction {
    bool is_static = false;

    NodeMethod(std::string qualified_name, NodeId id,
               std::vector<std::string> attrs, std::string short_name,
               NodeTypePtr && return_type, std::vector<Param> && params,
               bool is_static)
        : NodeFunction(qualified_name, id, attrs, short_name,
                       std::move(return_type), std::move(params)),
          is_static(is_static) {
        kind = NodeKind::Method;
    }
};

//------------------------------------------------------------------------------
// Field
//------------------------------------------------------------------------------
struct Field {
    std::string name;
    NodeTypePtr type;
};

//------------------------------------------------------------------------------
// NodeRecord
//------------------------------------------------------------------------------
struct NodeRecord : public NodeAttributeHolder {
    std::vector<Field> fields;
    std::vector<NodeMethod> methods;

    uint32_t size;
    uint32_t align;
    bool force_alignment;

    NodeRecord(std::string qualified_name, NodeId id,
               std::vector<std::string> attrs,
               uint32_t size, uint32_t align)
        : NodeAttributeHolder(qualified_name, id, NodeKind::Record, attrs),
          size(size), align(align), force_alignment(false) {}
};

//------------------------------------------------------------------------------
// Root
//------------------------------------------------------------------------------
struct Root {
    std::vector<TranslationUnit> tus;

    Root(std::vector<TranslationUnit> && tus)
        : tus(std::move(tus))
    {}

    Root(Root && rhs)
        : tus(std::move(rhs.tus))
    {}

    void operator==(Root && rhs) {
        tus = std::move(rhs.tus);
    }
};

} // namespace cppmm
