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

#include <vector>


struct HLNode;

enum MLCmd
{
    // 0 .. OP_MAX: operands
    ML_DECL = OP_MAX,
    ML_DECL_NS,
    ML_CLOSE,
    ML_IFELSE,
    ML_WHILE,
    ML_FOR,
    ML_FNCALL,
    ML_FUNCDEF_BEGIN,
    ML_FUNCDEF_BODY,
    ML_FUNCDEF_END,
    ML_NEW_1,
    ML_NEW_2,
    ML_LOADK,   // load from constant table
    ML_INDEX,
};

enum MLSymbolRefContext
{
    SYMREF_STANDARD = 0x00,  // Just any symbol (plain old variable, no special properties)
    SYMREF_MUTABLE  = 0x01,  // Can assign to this symbol
    SYMREF_TYPE     = 0x02,  // Symbol is used as a type
    SYMREF_CALL     = 0x04,  // Symbol is called (-> symbol is function-ish)
    SYMREF_INDEX    = 0x08,  // Symbol is indexed (sym.thing or sym[])
};


// When parsing the ML code, we keep a stack of symbols and refer to those via index
// External symbols are at the top and stay constant
// Anything that follows is pushed and popped as scopes change

// General rules:
// - every symbol has a known type (base types: type, function, any, ...)
// - if we know the symbol idx, we have a type
// - Complex types are resolved in a later step



struct MLDeclSym // create a symbol
{
    enum { Cmd = ML_DECL };
    u32 typeidx; // symbol idx of type
    u32 nameid;
};

struct MLDeclSymNS // create a symbol in a namespace
{
    enum { Cmd = ML_DECL_NS };
    u32 nsid;
    u32 typeidx; // symbol idx of type
    u32 nameid;
};

// Close up to 32 symbols at once (ie. end of their scope was reached)
struct MLCloseSyms
{
    enum { Cmd = ML_CLOSE };
    u32 n; // > 0, < 32
    u32 close; // bitmask; if set, identifier was captured as upvalue and must be moved to the heap
};

struct MLConditional
{
    enum { Cmd = ML_IFELSE };
    u32 numOpsCond;
    u32 numOpsIfBranch;
    u32 numOpsElseBranch;
};

struct MLWhile
{
    enum { Cmd = ML_WHILE };
    u32 numOpsCond;
    u32 numOpsBody;
};

struct MLFor
{
    enum { Cmd = ML_FOR };
    u32 numIters;
    u32 numOpsBody;
};

struct MLFnCall
{
    enum { Cmd = ML_FNCALL };
    u32 symidx; // symbol idx of function to call
    u32 nargs;
    u32 nret;
};

// Protocol: Emit MLDeclSym[nargs] right afterward, then MLDeclSym[nret] for return types
struct MLFuncDefBegin
{
    enum { Cmd = ML_FUNCDEF_BEGIN };
    u32 nargs; // number of symbols
    u32 nret;  //
    u32 flags; // TODO: vararg params, vararg return
    // args[], rets[] follows (each is a symbol ID)
};

struct MLFuncDefBody
{
    enum { Cmd = ML_FUNCDEF_BODY };
};


struct MLFuncDefEnd
{
    enum { Cmd = ML_FUNCDEF_END };
};

struct MLNew1
{
    enum { Cmd = ML_NEW_1 };
    u32 type;
};

struct MLNew2
{
    enum { Cmd = ML_NEW_2 };
    u32 prealloc;
    u32 type;
};

struct MLLoadK
{
    enum { Cmd = ML_LOADK };
    u32 validx; // index in constant table
};


class MLIRContainer
{
public:
    MLIRContainer(GC& gc);


    GaffaError import(HLNode *root, const StringPool& pool, const char *fn);


    Symstore syms;

private:
    template<typename T> void marker()
    {
        const size_t N = sizeof(T) / sizeof(unsigned);
        static_assert(N == 0, "oops");
        _add(NULL, 0, T::Cmd);
    }
    template<typename T> void add(const T& t)
    {
        const size_t N = sizeof(T) / sizeof(unsigned);
        static_assert(N, "oops");
        const unsigned *p = reinterpret_cast<const unsigned*>(&t);
        _add(p, N, T::Cmd);
    }
    template<typename T> void addNoHdr(const T& t)
    {
        const size_t N = sizeof(T) / sizeof(unsigned);
        static_assert(N, "oops");
        const unsigned *p = reinterpret_cast<const unsigned*>(&t);
        _addRaw(p, N);
    }

    enum PrepassResult
    {
        SKIP_CHILDREN     = 0x1,
    };

    void _add(const unsigned *p, const size_t n, unsigned cmd);
    void _addRaw(const unsigned *p, const size_t n);
    void _processRec(HLNode *n);
    ScopeType _precheckScope(HLNode *n);
    unsigned  _pre(HLNode *n); // -> PrepassResult
    void _post(HLNode *n);
    void _declWithType(HLNode *n, MLSymbolRefContext ref, unsigned typeidx);
    void _declInNamespace(HLNode *n, MLSymbolRefContext ref, unsigned typeidx, HLNode *namespac);
    unsigned _refer(HLNode *n, MLSymbolRefContext ref); // returns symbol index of type, 0 for auto-deduct
    void _codegen(HLNode *n);

    void _addLiteralConstant(ValU v);

    std::vector<unsigned> mops;
    const StringPool *curpool;
    StringPool symbolnames;
    ValStore literalConstants;
    const char *_fn;
};


