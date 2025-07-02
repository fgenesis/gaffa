// Medium-level intemediate representation
// This is intended to be serializable and loadable, with the following considerations:
// - instantiate LLIR by linking MLIR with a symbol table (aka environment)
// - Because we don't necesarily have all the symbols yet, we can't do constant folding
// - But we can figure out which symbols must be types

#pragma once

#include "gainternal.h"
#include "symstore.h"
#include "strings.h"
#include "valstore.h"
#include "gaalloc.h"

#include <vector>


struct HLNode;

// TODO: Eventually, this chould become the output of the parser. Drop the intermediate HL* stuff.
// Move variable tracking and scoping logic to the lowering stage

// TODO: remove distinction in this stage for ML_REFSYM vs ML_REFUPVAL?

enum MLCmd
{
    ML_NOP = 0, // This is the only one to record the number of children
    ML_GROUP_BEGIN,
    ML_GROUP_END,
    ML_UNOP,
    ML_BINOP,
    ML_DECL,   // container for the next few
        ML_CONST,  // declare immutable symbol; deduce type
        ML_TCONST, // declare immutable symbol; type is given
        ML_VAR,    // declare mutable symbol; deduce type
        ML_TVAR,   // declare mutable symbol; type is given
    ML_FORGET, // forget symbols
    ML_DECL_NS,// declare symbol in namespace
    ML_REFLOCAL,
    ML_REFSYM, // external symbol lookup
    ML_REFUPVAL, // upvalue lookup
    ML_ASSIGN, // TODO: how to setupval
    ML_CLOSEUPVAL,
    ML_IF,
    ML_IFELSE,
    ML_WHILE,
    ML_FOR,
    ML_FNCALL,
    ML_MTHCALL,
    ML_FUNCDEF,
    ML_NEW_ARRAY,
    ML_NEW_TABLE,
    ML_LOADK,   // load from constant table
    ML_INDEX,
    ML_RETURN,
    ML_YIELD,
    ML_EMIT,

    ML_END,
};


// When parsing the ML code, we keep a stack of symbols and refer to those via index
// External symbols are at the top and stay constant
// Anything that follows is pushed and popped as scopes change

// General rules:
// - every symbol has a known type (base types: type, function, any, ...)
// - if we know the symbol idx, we have a type
// - Complex types are resolved in a later step

#if 0

struct MLNodeBase
{
    const u32 *payload() const { return &cmd; }
    //void nopOut();

    struct
    {
        MLNodeBase *next;
        MLNodeBase *child; // first child, further children are in child->next and so on
        u32 numvals; // At least 1 because cmd is included
        u32 xch; // expected number of child nodes that follow (chain starting in this->child)
        u32 nch; // actual number of child nodes that follow
    } hdr;

    // -- anything below here is serialized (N x u32, where cmd is the first) --
    u32 cmd;
    // (members of derived nodes follow)
};

// Used ONLY to group multiple instructions together (scoping is handled elsewhere)
// It's also the ONLY block to have a variable number of children
struct MLBlock : public MLNodeBase
{
    enum { Cmd = ML_NOP, N = 0, Ch = -1 };
};

struct MLUnOp: public MLNodeBase
{
    enum { Cmd = ML_UNOP, N = 0, Ch = 1 };
    u32 op;
};

struct MLBinOp: public MLNodeBase
{
    enum { Cmd = ML_BINOP, N = 0, Ch = 2 };
    u32 op;
};

struct MLSymBase : public MLNodeBase
{
    enum { N = 1 };
    u32 nameid;
};

struct MLVar : public MLSymBase
{
    enum { Cmd = ML_VAR, Ch = 0 };
};

struct MLTVar: public MLSymBase
{
    enum { Cmd = ML_TVAR, Ch = 1 };
    // child[0]: expr that yields type
};

struct MLConst : public MLSymBase
{
    enum { Cmd = ML_CONST, Ch = 1 };
    // child[0]: expr that yields value
};

struct MLTConst: public MLSymBase
{
    enum { Cmd = ML_TCONST, Ch = 2 };
    // child[0]: expr that yields type
    // child[1]: expr that yields value
};

struct MLDeclNS : public MLSymBase
{
    enum { Cmd = ML_DECL_NS, N = 2, Ch = 1 };
    u32 nsidx; // symbol idx of namespace ident
    // child[0]: expr that yields value
};

struct MLGetSymval : public MLNodeBase
{
    enum { Cmd = ML_REFSYM, N = 1, Ch = 0 }; // also ML_REFUPVAL
    u32 symidx; // < 0: sym in env, otherwise: local symbol index
};

struct MLCloseUpval : public MLNodeBase
{
    enum { Cmd = ML_CLOSEUPVAL, N = 1, Ch = 0 };
    u32 upvalidx;
};

struct MLForget : public MLNodeBase
{
    enum { Cmd = ML_FORGET, N = 1, Ch = 0 };
    u32 n;
};

struct MLReturnIsh : public MLNodeBase
{
    enum { Cmd = ML_RETURN, N = 0, Ch = 1 }; // also ML_YIELD, ML_EMIT
    // child[0]: expr
};

struct MLIf : public MLNodeBase
{
    enum { Cmd = ML_IF, N = 0, Ch = 2 };
    // child[0]: cond
    // child[1]: if-part
};

struct MLIfElse : public MLNodeBase
{
    enum { Cmd = ML_IFELSE, N = 0, Ch = 3 };
    // child[0]: cond
    // child[1]: if-part
    // child[2]: else-part
};

struct MLWhile : public MLNodeBase
{
    enum { Cmd = ML_WHILE, N = 0, Ch = 2 };
    // child[0]: cond
    // child[1]: body
};

struct MLFor : public MLNodeBase
{
    enum { Cmd = ML_FOR, N = 0, Ch = 2 };
    // child[0]: (block of) decls with iterators
    // child[1]: body
};

struct MLFnCall : public MLNodeBase
{
    enum { Cmd = ML_FNCALL, N = 0, Ch = 2 };
    // child[0]: expr that is called
    // child[1]: params
};

struct MLFuncDef : public MLNodeBase
{
    enum { Cmd = ML_FUNCDEF, N = 0, Ch = 3 };
    // child[0]: args: decl or block of decls
    // child[1]: expr that yields returns type
    // child[2]: body
};

struct MLNewArray : public MLNodeBase
{
    enum { Cmd = ML_NEW_ARRAY, N = 1, Ch = 0 };
    u32 prealloc;
};

struct MLNewTable : public MLNodeBase
{
    enum { Cmd = ML_NEW_TABLE, N = 1, Ch = 0 };
    u32 prealloc;
};

struct MLLoadK : public MLNodeBase
{
    enum { Cmd = ML_LOADK, N = 1, Ch = 0 };
    u32 validx; // index in constant table
};

struct MLAssign : public MLNodeBase
{
    enum { Cmd = ML_ASSIGN, N = 0, Ch = 2 };
    // child[0]: target exprs
    // child[1]: source exprs
};


class MLIRChainBuilder
{
public:
    MLIRChainBuilder(BlockListAllocator& bla, MLNodeBase *rootnode);
    ~MLIRChainBuilder();

    template<typename T>
    T *newnode()
    {
        return static_cast<T*>(_allocnode(sizeof(T), T::N, T::Cmd, T::Ch));
    }

    template<typename T>
    T *add()
    {
        T *n = newnode<T>();
        if(n)
            append(n);
        return n;
    }

    void pop(); // Check for exact # of children
    void popBlock(); // Pop without checking
    void _pop();
    //void tryFinishChain();
    void append(MLNodeBase *n);

    MLNodeBase *getRootNode();
    MLNodeBase *getFirstOverlayNode();

private:
    MLNodeBase *_allocnode(u32 sz, u32 n, u32 cmd, u32 nch);

    void _push(MLNodeBase *n); // current node becomes a parent, newly added nodes start a new chain

    MLNodeBase *lastnode;
    MLNodeBase * const rootnode;
    PodArray<MLNodeBase*> blockstack;
    BlockListAllocator& bla;
};

#endif

class MLIRContainer
{
public:
    MLIRContainer(GC& gc);


    GaffaError import(HLNode *root, const StringPool& pool, const char *fn);



    Symstore syms;

private:
    //MLIRChainBuilder *currentChain();
    //MLIRChainBuilder *_chain;


    //BlockListAllocator bla;


    ScopeType _precheckScope(HLNode *n);
    void  _handleNode(HLNode *n);
    void _handleChildren(HLNode *n);
    //void _handleChildrenOf(HLNode *n, MLNodeBase *parent);
    void _declWithType(HLNode *n, MLSymbolRefContext ref, HLNode *typeexpr, bool checkdefined);
    void _declInNamespace(HLNode *n, MLSymbolRefContext ref, HLNode *namespac);
    Symstore::Lookup _refer(HLNode *n, MLSymbolRefContext ref);
    void _getvalue(HLNode *n, MLSymbolRefContext ref);

    void _addLiteralConstant(ValU v);

    /*template<typename T>
    inline T *add()
    {
        return currentChain()->add<T>();
    }*/

    size_t add(u32 x);
    void add(const u32 *p, size_t n);
    u32 *addn(size_t n);

    template<size_t N>
    FORCEINLINE void add(u32(&a)[N]) { add(&a[0], N); }


    PodArray<u32> oplist;
    GC& gc;


    const StringPool *curpool;
    StringPool symbolnames;
    ValStore literalConstants;
    const char *_fn;

    /*class ChainOverlay
    {
    public:
        ChainOverlay(MLIRContainer *self, MLNodeBase *rootnode);
        ~ChainOverlay();
        MLIRChainBuilder chain;
    private:
        MLIRContainer * const _self;
        MLIRChainBuilder * const _oldch;
    };*/
};


