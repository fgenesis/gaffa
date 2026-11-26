#pragma once

#include "defs.h"
#include "array.h"
#include "table.h"
#include "typing.h"
#include "gaobj.h"

struct HLNode;
struct BufSink;
class StringPool;
class Symstore;
class VM;

// Rules:
// exprs that survive folding must have <= 1 params, since MLNode::m.p[1] is overlaid with the deduced type
// stmts can have up to 2 params
//
// Notes:
// typeexpr empty means untyped or deduce automatically
// ...idx is the index of the first affected slot, others follow consecutively

enum MLCmd
{
    ML_LIST = 0,        // always stmt

    // Operators first. These are known to be unary or binary, so either 1 or 2 child nodes follow.
    // Never lists for regular operators.
    // In case of OP_CALL, the rhs may be a list of params.
    _ML_OP_FIRST = 1,   // always expr
    _ML_OP_MAX = _OP_MAX,

                        // kind (#params) [#children]

    ML_CONST = _OP_MAX, // expr (1, consts idx)
    ML_VAR,             // expr (1, var idx)
    ML_EXT,             // expr (1, external table idx)
    ML_NAMEDECL,        // stmt (2, name, symid) [2, namespace, value] -- namespaced (func T.f / T:f): (2, name, varidx) of a hidden var; no namespace: empty list
    ML_DECL,            // stmt (1, varidx) [2, typeexprs, exprs] -- no exprs: deferred
    ML_ASSIGN,          // stmt (0) [2, dstlist, exprlist]
    ML_IFELSE,          // stmt (0) [3, cond, ifblock, elseblock]
    ML_WHILE,           // stmt (0) [2, cond, block]
    ML_FOR,             // stmt (0) [2, iterdecls, block] -- one ML_ITERDECL per iterator
    ML_GETINDEX,        // expr (0) [3, obj, key, func] -- obj[key], obj.name
    ML_GETNS,           // expr (0) [2, ns, key]
    ML_MTHCALL,         // expr (0) [3, selfexpr, funcexpr, paramexprs] -- self:name(params)
    ML_FUNC,            // expr (1, varidx) [3, argtypes, rettypes, block] -- varidx = start of locals
    ML_RETURN,          // stmt (0) [1, exprs]
    ML_EMIT,            // stmt (0) [1, exprs]
    ML_ITERPACK,        // expr (0) [1, exprs]
    ML_NEW_ARRAY,       // expr (0) [1, exprs]
    ML_NEW_TABLE,       // expr (1) (numkv) [1, exprlist] // {a=1, b=2, 3, 4} -> [a, 1, b, 2, 3, 4], numkv=2
    ML_EXPORT,          // stmt (0) [1, exprs] // ML_NAMEDECL or ML_VAR following
    ML_CALLADJ,         // expr (0) [2, numexpr, callexpr] -- f()#N, ...#N
    ML_SINK,            // target (1, kind) [1, typeexpr] -- typeexpr is _ML_EMPTY if untyped
    ML_BLOCK,           // stmt (0) [1, stmts] -- explicit block scope, only used if not handled by mlOpensBlock().
    ML_IMPORT,          // expr (0) [1, expr]
    ML_COMPTIME,        // expr (0) [1, expr]
    ML_OPTIONAL,        // expr (0) [1, typeexpr] -- T -> T?
    ML_UNWRAPBIND,      // expr (1, varidx) [1, expr] -- if(x => v)
    ML_VALBLOCK,        // expr (0) [1, body] -- $stmt:
    ML_ITERDECL,        // stmt (2, varidx, hiddenidx) [3, typeexprs, iterexpr, iterfunc] -- m.flags = number of values per iteration.
    ML_ITERDECLVARIADIC,// stmt (2, varidx, hiddenidx) [1, typeexprs] -- for(decl = ...); m.flags = number of values per iteration.
    ML_RANGE,           // expr (0) [3, begin, end, step] -- only as child of ML_ITERDECL;
    ML_VARARGS,         // expr (0) [0]
    ML_VARARGCOUNT,     // expr (0) [0] -- #...
    ML_SETINDEX,        // target (0) [3, obj, key, func] -- obj[key] / obj.name as an ML_ASSIGN target
    ML_BREAK,           // stmt (0) [0]
    ML_CONTINUE,        // stmt (0) [0]

    _ML_MAX,

    // (Trying to keep the 7th bit and up free for more efficient encoding)

    // Below: Internal, not serialized
    _ML_UPVAL = _ML_MAX,// expr (1, func's upvals idx)
    _ML_LOCAL,          // expr (1, func's locals idx)
    _ML_FUNCIDX,        // expr (1, func idx)  // function + mgmt infos. Recursion breaker.
    _ML_EMPTY,          // expr (0) placeholder, eg. the type of 'var x'; exprtype is PRIMTYPE_AUTO.
                        // Serialized as an empty list. Unambiguous: a type list is never empty where an empty type can appear
                        // (a decl has >= 1 target; params always have a type). Internally needed because a list
                        // of 1 is the child itself, so a single empty type would make the whole list empty.

    _ML_DEAD, // Node was optimized away and is no longer valid
    _ML_VAL, // constant value, stored inline in MLNode
    _ML_UVAR, // Unresolved identifier // special; replaced before type analysis
    _ML_ERROR, // Error (p[0] = message). If encountered after folding, the tree isn't sound and no code can be generated.
               // p[1] != 0: a fold-time exception (eg. division by zero), p[1] is its plain message; '??' captures it.
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

bool mlirIsStmt(MLCmd cmd); // also true for sinks: no value

enum MLFuncFlags // must fit in a byte
{
    MLFUNC_CONST = 0x01, // [const]: the function type has Const(T) as first param, the param var stays T
    MLFUNC_PURE  = 0x02, // [pure]
};

enum MLNodeFlags // MLNode::m.flags of any expr except ML_FUNC (which uses MLFuncFlags)
{
    // The value's static type is the node's exprtype (a type list if it produces several values), which isn't
    // known statically: it must be checked at runtime (LLIR emits LL_TYPECHECK). Set by fitValue() for an
    // 'any' whose value isn't known. Not serialized: the result of folding.
    MLNODE_RTCHECK = 0x80,

    // The expr gives any number of values (T...), and its statement takes a fixed number of them (fixed targets or
    // params). Folding sets its exprtype to the values the statement uses: its fixed values, then one entry per value
    // taken from the variadic part, then T... only if more values are fine (a '___', ASSIGN_DROP_EXCESS, variadic
    // params). LLIR pads missing values with nil, checks at runtime that the entries that aren't optional are there,
    // and without a trailing T... that there are no more values. Not serialized.
    // On an ML_CALLADJ (N values from a variadic part), extra values are always dropped.
    MLNODE_VARTAKE = 0x40,
};

// Hidden vars of an ML_ITERDECL (from its hiddenidx), allocated by folding: ML_ITER_HIDDEN state vars (the most
// any iterator needs), then one scratch var per value that goes into a sink ('_', or each value a '___' takes).
// They're mutable and never referenced by MLIR nodes. Split pass 1 gives an iterator consecutive local slots:
// the state vars its kind uses (LLIR may use fewer, eg. a range without a step), then one per value in order
// (the loop var, or the scratch var for a sink). So LLIR only needs the first slot.
enum MLIterHidden
{
    ML_ITER_HIDDEN = 3,
    ML_ITER_NOHIDDEN = 0xffffffff, // hiddenidx before folding

    // ML_RANGE: all three have the element type
    ML_ITER_RANGE_CUR = 0,
    ML_ITER_RANGE_END = 1,
    ML_ITER_RANGE_STEP = 2,
    ML_ITER_RANGE_USED = 3,

    // Object iteration: obj has the iterated expr's type, idx is a uint passed to T::__iterN
    ML_ITER_OBJ_OBJ = 0,
    ML_ITER_OBJ_IDX = 1,
    ML_ITER_OBJ_USED = 2,

    // ML_ITERDECLVARIADIC: index and end, both uint
    ML_ITER_VARARGS_IDX = 0,
    ML_ITER_VARARGS_END = 1,
    ML_ITER_VARARGS_USED = 2,
};

enum MLSinkKind // ML_SINK p[0]
{
    ML_SINK_ONE = 0,      // '_': takes one value
    ML_SINK_WIDE = 1,     // '___': takes the remaining values
    ML_SINK_VARIADIC = 2, // '...' (or 'T ...') ending a function's parameter list: its variadic params, which have no var.
                          // typeexpr is T; _ML_EMPTY for an untyped '...' (any).
};

enum MLDeclFlags // MLNode::m.flags of ML_DECL
{
    // No values before folding: 'x = defer'. Set by folding before the values are folded, since values may
    // fold to nothing (eg. a value block that emits nothing). Not serialized.
    MLDECL_DEFERRED = 0x01,
};

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
        byte nch;   // number of direct child nodes. Optional, but makes LLIR generation easier.
        byte flags; // ML_FUNC: MLFuncFlags; other exprs: MLNodeFlags
        // 1 unused byte
    } m;
    struct
    {
        u32 len;
    } list;
    struct
    {
        u32 _do_not_use; // same as p[0]; some exprs have a parameter, using this would clobber it
        Type exprtype; // any expr node stores its known type here. This is fine because no expr has 2 params.
    } x;
    struct
    {
        const HLNode *hlnode; // Temporarily stored during construction
    } tmp;

    MLNode *firstChild();
    const MLNode *firstChild() const;
    Val asVal() const;
    void setVal(const ValU &v);
    size_t numchildren() const;
    MLSub aslist(); // Returns children, or itself if not list (as if it was a list with 1 child)
    Type type() const;
    bool isconst() const; // Is a const value for the puspose of constant folding?
    void setError(sref s);
    void setError(StringPool& sp, const char *err);
    void setException(sref err, sref what); // an error that '??' can capture as an error value with message 'what'
    sref exceptionWhat() const { return m.cmd == _ML_ERROR ? m.p[1] : 0; }

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
    enum Kind
    {
        // --- serialized ---
        CVAR,       // Local variable, not mutable (its value may be known while folding, see 'known')
        MUTVAR,     // Local variable, mutable
    };
    Kind kind;
    sref name;
    // -- Nothing below here is serialized --
    u32 decl; // if != 0: node index of node that declares us

    union
    {
        ValU val; // unused
        struct
        {
            _AnyValU _pad;
            Type type; // the var's (static) type; PRIMTYPE_AUTO if unknown
        } local;
    } u; // Arranged so that val.type and local.type overlap

    // A CVAR's value if it's known while folding (PRIMTYPE_NOTYPE if not), see knownValue(). References to it
    // stay var refs (locals stay locals in the generated code); the value is only used for folding.
    ValU known;

    inline bool isMutable() const { return kind == MUTVAR; }
};

struct MLExternal
{
    sref name;
    Val val;
};

struct MLPreVisitResult
{
    VisitResult res;
    uintptr_t aux;
};
typedef MLPreVisitResult (*MLVisitorPre)(MLNode *node, MLNode *parent, void *ud);
typedef void (*MLVisitorPost)(MLNode *node, MLNode *parent, void *ud, uintptr_t aux);

// Whether a node opens a block (a scope for imports and namespaced locals) when visited as a child of parent:
// explicit ML_BLOCKs, the root (visited as its own parent), functions, value blocks, for loops (iterators + body),
// each branch of an if, and the body of a while.
bool mlOpensBlock(const MLNode *node, const MLNode *parent);


struct MLUpvalRef
{
    u32 varidx; // Index into MLIR::vars[]
    u32 slot;

    // if false, this is a local in the enclosing function, and slot is the index in MLFunc::locals
    // if true, this refers to the upvalue slot of the enclosing function, and slot is the index in MLFunc::upvals
    bool transient;
};



struct MLFunc
{
    inline MLFunc() : myidx(-1), name(0), enclosingFuncIdxPlus1(0) {}
    size_t myidx; // index of this node in MLIR::nodes[]
    MLNode node; // A copy of the original MLNode
    sref name;
    size_t enclosingFuncIdxPlus1; // If 0, this is the root function, ie. file scope
    PodArray<u32> locals; // The first info.nargs entries are params, the rest declared locals
    PodArray<MLUpvalRef> upvals;
    DFunc *dfunc;
};

class MLIR
{
public:
    MLIR(GC& gc);
    ~MLIR();

    enum Options // bitmask
    {
        DEFAULT = 0,
        STRIP_DEBUGINFO = 1
    };

    // Construct a MLNode tree out of a HLNode tree.
    void construct(const HLNode *root, Symstore& syms, StringPool& sp, Options options);


    // Typecheck and optimize the tree.
    bool fold(VM& vm, Symstore& syms, SymTable &extSyms);

    size_t indexOf(const MLNode *node) const;
    size_t indexOf(const MLVar *v) const;
    const MLInfo *infoOf(const MLNode *node) const;

    MLVar *getVarFromSymid(size_t symid);

    void visit(MLVisitorPre pre, MLVisitorPost post, void *ud);
    void dump(BufSink *sink, const StringPool& sp, Options options) const;

    PodArray<MLNode> nodes;
    PodArray<MLInfo> infos;
    PodArray<MLVar> vars;
    PodArray<MLExternal> externals;
    PodArray<MLFunc> funcs;

    PodArray<u32> symToVarRemap; // Maps symbol index to index in vars[]

    GC& gc;

    void convertNode(MLNode& m, const HLNode& h, MLNode *parent);
    void convertList(HLNode& list);
    void convertVarDef(MLNode& m, HLNode& h);

private:
    struct Cons
    {
        const HLNode *hl;
        size_t mlidx;
        unsigned flags; // ConsFlags
    };
    enum ConsFlags
    {
        CONS_BODY = 1,      // body of a node that opens a block implicitly: a HLNODE_BLOCK here doesn't need an ML_BLOCK
        CONS_NOTYPEMOD = 2, // an identifier's '?' was already handled (it's the child of the ML_OPTIONAL)
        CONS_ITERDECL = 4,  // a declaration in for(...): becomes an ML_ITERDECL
    };

    struct MLCh
    {
        size_t chIdx; // index of first child
        size_t n;
    };

    void _setupVars(Symstore& syms, StringPool& sp);
    void _resolveVars(const size_t *unresolved, size_t n, Symstore& syms);

    void _construct(Queue<Cons>& q, MLNode *dst, const HLNode *hl, unsigned flags, StringPool& sp); // may reallocate dst
    void _cons(Queue<Cons>& q, MLNode *dst, const HLNode *hl, unsigned flags = 0);

    MLNode *_add(size_t n); // add a couple nodes to the end as one block; points to first node.
    MLCh _setupCh(MLNode *& node, MLCmd cmd); // assign children to node. may reallocate and invalidate note.
    MLCh _setupList(MLNode *& node, size_t n); // makes node a list, returns ptr to start of list elems. may reallocate and invalidate note.
    MLCh _setupChDefault(Queue<Cons>& q, MLNode *node, const HLNode *hl, MLCmd cmd);

    u32 _decllist(Queue<Cons>& q, MLNode *& dst, const HLNode *decllist, const StringPool& sp);
    void _opr(Queue<Cons>& q, MLNode *& dst, OperatorId op, const HLNode *hl);
    void _sink(Queue<Cons>& q, MLNode *& dst, const HLNode *sink, const HLNode *type);
};
