#pragma once

#include "defs.h"
#include "array.h"
#include "typing.h"

struct HLNode;

enum MLCmd
{
    // < 0: list of (-n + 1) children -- it's intentionally impossible to construct lists of length 1

    ML_LIST = 0,        // always stmt

    // Operators first. These are known to be unary or binary, so either 1 or 2 child nodes follow.
    _ML_OP_FIRST = 1,   // always expr
    _ML_OP_MAX = _OP_MAX,

                        // kind (#params) [#children]

    ML_CONST = 40,      // expr (1, const table idx)
    ML_VAR,             // expr (1, local table idx) -- local, upval, or extval
    ML_NAMEDECL,        // stmt (1, name) [2, namespace, value]
    ML_DECL,            // stmt (2, local start, N) [2, typeexprs, exprs]
    ML_CLOSE,           // stmt (2, local start, N)
    ML_ASSIGN,          // stmt (0) [2, dstlist, exprlist]
    ML_IFELSE,          // stmt (0) [3, cond, ifblock, elseblock]
    ML_WHILE,           // stmt (0) [2, cond, block]
    ML_FOR,             // stmt (0) [2, decls, block]
    ML_FNCALL,          // expr (0) [2, funcexpr, paramexprs]
    ML_MTHCALL,         // expr (0) [3, selfexpr, funcexpr, paramexprs]
    ML_FUNC,            // expr (0) [3, decls, rets, block]
    ML_RETURN,          // stmt (0) [1, exprs]
    ML_YIELD,           // stmt (0) [1, exprs]
    ML_EMIT,            // stmt (0) [1, exprs]
    ML_ITERPACK,        // expr (0) [1, exprs]
    ML_NEW_ARRAY,       // expr (0) [1, exprlist]
    ML_NEW_TABLE,       // expr (0) [1, keylist, exprlist]
    ML_EXPORT,          // stmt (0) [1, exprs] // ML_NAMEDECL following

    // (Trying to keep the 7th bit and up free for more efficient encoding)

    // Below: Internal, not serialized

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

/* ALTERNATIVE
u32 v0 -- large enough for indices
u16 v1 -- large enough for counts
byte cmd
u32 chOffs
- store values always in constant table
-- but 16b is a nice size, easy to index. this would be 12b

OR
add u16 typeidx
each node that has a type but no space to store one should index into types array
can store primtypes directly
operators can store type in p[0]
vars and values fetch types from their own table

*/

union MLNode
{
    ValU val;       // if _ML_VAL (known to not have children, no params)
    struct
    {
        u32 p[2];
        u32 chOffs; // must be usable together with hl.node, HL_LIST, others. _ML_VAL conflicts.
        byte cmd;
        // u16 typeidx
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
        u32 _do_not_use; // some exprs have a parameter, using this would clobber it
        Type exprtype; // any expr node stores its known type here. This is fine because no expr has 2 params.
    } x;

    MLNode *firstChild();
    Val asVal();
};

struct MLInfo
{
    u32 line, column;
};

struct MLTypeInfo
{
    Type t;
};

class MLVarStore
{
};

struct MLFoldTracker
{
};

typedef uintptr_t (*MLVisitorPre)(MLNode *node, MLNode *parent);
typedef void (*MLVisitorPost)(MLNode *node, MLNode *parent, uintptr_t aux);

class MLIRBuilder
{
public:
    MLIRBuilder(GC& gc);

    // Construct a MLNode tree out of a HLNode tree.
    // The generated MLNodes are unresolved (cmd == _ML_HL_TODO) and still point to their HLNode.
    void construct(const HLNode *root);

    // Convert each node with cmd == _ML_HL_TODO fully into an MLNode.
    // The HLNode tree is no longer needed after this call.
    void convert();

    // Typecheck and optimize the tree. 
    void fold(MLFoldTracker& ft);

    size_t indexOf(MLNode *node) const;

    void visit(MLVisitorPre pre, MLVisitorPost post);

    PodArray<MLNode> nodes;
    PodArray<MLInfo> infos;


    MLVarStore vars;

    GC& gc;


};
