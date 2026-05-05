#pragma once

#include "defs.h"
#include "array.h"
#include "typing.h"

struct HLNode;
struct BufSink;
class StringPool;
class Symstore;

enum MLCmd
{
    ML_LIST = 0,        // always stmt

    // Operators first. These are known to be unary or binary, so either 1 or 2 child nodes follow. Never lists.
    _ML_OP_FIRST = 1,   // always expr
    _ML_OP_MAX = _OP_MAX,

                        // kind (#params) [#children]

    ML_CONST = 40,      // expr (1, const table idx)
    ML_VAR,             // expr (1, local table idx) -- local, upval, or extval
    ML_NAMEDECL,        // stmt (1, name) [2, namespace, value]
    ML_DECL,            // stmt (1, local start) [2, typeexprs, exprs] -- num of vars = #typeexprs
    ML_CLOSE,           // stmt (2, local start, N)
    ML_ASSIGN,          // stmt (0) [2, dstlist, exprlist]
    ML_IFELSE,          // stmt (0) [3, cond, ifblock, elseblock]
    ML_WHILE,           // stmt (0) [2, cond, block]
    ML_FOR,             // stmt (0) [2, decls, block]
    ML_FNCALL,          // expr (0) [2, funcexpr, paramexprs]
    ML_MTHCALL,         // expr (0) [3, selfexpr, funcexpr, paramexprs]
    ML_FUNC,            // expr (1, locals start) [3, argtypes, rettypes, block]
    ML_RETURN,          // stmt (0) [1, exprs]
    ML_YIELD,           // stmt (0) [1, exprs]
    ML_EMIT,            // stmt (0) [1, exprs]
    ML_ITERPACK,        // expr (0) [1, exprs]
    ML_NEW_ARRAY,       // expr (0) [1, exprs]
    ML_NEW_TABLE,       // expr (1) (numkv) [1, exprlist] // {a=1, b=2, 3, 4} -> [a, 1, b, 2, 3, 4], numkv=2
    ML_EXPORT,          // stmt (0) [1, exprs] // ML_NAMEDECL or ML_VAR following
    //ML_VALBLOCK,

    // (Trying to keep the 7th bit and up free for more efficient encoding)

    // Below: Internal, not serialized
    _ML_EMPTY,
    _ML_DEAD, // Node was optimized away and is no longer valid
    _ML_VAL, // constant value, stored inline in MLNode
    _ML_HL_TODO // Refer to HLNode that must still be lowered
};

/*

(1 + 2) * f(3 + 4, a, b)

       MUL
PLUS            CALL
1 2        -3 PLUS a b
              3 4

MUL PLUS CALL 1 2 -3 PLUS a b 3 4
                  [         ]
  | ^  |    | ^~~       |     ^~~
  +-+  |    | |   ^     |     |
       +------+   |     +-----+
            +-----+
*/


union MLNode;

struct MLSub
{
    MLNode *ch; // index of first child
    size_t n;
};

/* Design decisions:
- Always stored in a single memory block that can me memmove()'d around
- All children of a parent are consecutive as one block in memory
- Children follow somewhere after parent
- As small as possible, esp. when encoded to storage format
- Must be fully reconstructible from storage format
- Larger things are referenced by ID and stored externally, this is just for
  keeping the original AST structure intact in a much more compact format than HLNode.
- No dynamically allocated memory in these nodes. Freeing the storage array must be enough.
- No pointers stored in nodes
- Can't reach parent from child nodes
*/
union MLNode
{
    ValU val;       // if _ML_VAL (known to not have children, no params)
    struct
    {
        u32 p[2];
        u32 chOffs; // must be usable together with hl.node, HL_LIST, others. _ML_VAL conflicts.
        byte cmd;
        // 3 unused bytes
    } m;
    struct
    {
        u32 len;
    } list;
    struct
    {
        const HLNode *node;
    } hl;
    struct
    {
        u32 _do_not_use; // same as p[0]; some exprs have a parameter, using this would clobber it
        Type exprtype; // any expr node stores its known type here. This is fine because no expr has 2 params.
    } x;

    MLNode *firstChild();
    const MLNode *firstChild() const;
    Val asVal() const;
    void setVal(const ValU &v);
    size_t numchildren() const;
    MLSub aslist(); // Returns children, or itself if not list (as if it was a list with 1 child)

    // Invalidate this node and all its children
    void invalidate();
    void makedummy();
};

struct MLInfo
{
    u32 line, column;
};

struct MLTypeInfo
{
    Type t;
};

struct MLVar
{
    enum Kind { LOCAL, UPVAL, EXT, CONSTVAL };
    Kind kind;
    sref name;
    union
    {
        ValU val; // if kind == CONSTVAL, this is the value, otherwise only its type is used
        u32 slot; // local slot, upvalue slot, etc.
    };
};


struct MLFoldTracker
{
};



struct MLPreVisitResult
{
    VisitResult res;
    uintptr_t aux;
};
typedef MLPreVisitResult (*MLVisitorPre)(MLNode *node, MLNode *parent, void *ud);
typedef void (*MLVisitorPost)(MLNode *node, MLNode *parent, void *ud, uintptr_t aux);

class MLIR
{
public:
    MLIR(GC& gc);
    ~MLIR();

    // Construct a MLNode tree out of a HLNode tree.
    // The generated MLNodes are unresolved (cmd == _ML_HL_TODO) and still point to their HLNode.
    void construct(const HLNode *root);

    // Convert each node with cmd == _ML_HL_TODO fully into an MLNode.
    // The HLNode tree is no longer needed after this call.
    void convert(const Symstore& syms, const StringPool& sp);

    // Typecheck and optimize the tree.
    void fold(MLFoldTracker& ft);

    size_t indexOf(MLNode *node) const;

    void visit(MLVisitorPre pre, MLVisitorPost post, void *ud);
    void dump(BufSink *sink, StringPool& sp) const;

    PodArray<MLNode> nodes;
    PodArray<MLInfo> infos;
    PodArray<MLVar> vars;

    GC& gc;

    void convertNode(MLNode& m, const HLNode& h, MLNode *parent);
    void convertList(HLNode& list);
    void convertVarDef(MLNode& m, HLNode& h);

private:
    struct Cons
    {
        const HLNode *hl;
        size_t mlidx;
    };

    struct MLCh
    {
        size_t chIdx; // index of first child
        size_t n;
    };

    void _construct(Queue<Cons>& q, MLNode *dst, const HLNode *hl); // may reallocate dst
    void _cons(Queue<Cons>& q, MLNode *dst, const HLNode *hl);

    MLNode *_add(size_t n); // add a couple nodes to the end as one block; points to first node.
    MLCh _setupCh(MLNode *& node, MLCmd cmd); // assign children to node. may reallocate and invalidate note.
    MLCh _setupList(MLNode *& node, size_t n); // makes node a list, returns ptr to start of list elems. may reallocate and invalidate note.
    void _delayExpand(MLNode *node, const HLNode *hl);

    sref _decllist(Queue<Cons>& q, MLNode *& dst, const HLNode *decllist);
    void _opr(Queue<Cons>& q, MLNode *& dst, OperatorId op, const HLNode *hl);
};
