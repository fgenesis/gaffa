// Medium-level intemediate representation
// This is intended to be serializable and loadable, with the following considerations:
// - instantiate LLIR by linking MLIR with a symbol table (aka environment)
// - Because we don't necesarily have all the symbols yet, we can't do constant folding
// - But we can figure out which symbols must be types

#pragma once

#include "gainternal.h"
#include "symstore.h"
#include "strings.h"

#include <vector>


struct HLNode;

enum MLCmd
{
    ML_DECL,
    ML_CLOSE,
    ML_VAL,
    ML_OP,
};

enum MLSymbolRefContext
{
    SYMREF_STANDARD = 0x00,  // Just any symbol (plain old variable, no special properties)
    SYMREF_MUTABLE  = 0x01,  // Can assign to this symbol
    SYMREF_TYPE     = 0x02,  // Symbol is used as a type
    SYMREF_CALL     = 0x04,  // Symbol is called (-> symbol is function-ish)
    SYMREF_INDEX    = 0x08,  // Symbol is indexed (sym.thing or sym[])
    SYMREF_HASMTH   = 0x10,  // Symbol is used in a method call context (sym:method())
};


// When parsing the ML code, we keep a stack of symbols and refer to those via index
// External symbols are at the top and stay constant
// Anything that follows is pushed and popped as scopes change

// General rules:
// - every symbol has a known type
// - if we know the symbol idx, we have a type



struct MLDeclSym // create a symbol
{
    enum { Cmd = ML_DECL };
    unsigned typeidx; // symbol idx of type
    unsigned nameid;
};

// Close up to 32 symbols at once (ie. end of their scope was reached)
struct MLCloseSyms
{
    enum { Cmd = ML_CLOSE };
    unsigned n; // > 0, < 32
    unsigned close; // bitmask; if set, identifier was captured as upvalue and must be moved to the heap
};

struct MLConditional
{
    unsigned numOpsCond;
    unsigned numOpsIfBranch;
    unsigned numOpsElseBranch;
};

struct MLWhile
{
    unsigned numOpsCond;
    unsigned numOpsBody;
};

struct MLFnCall
{
    unsigned symidx; // symbol idx of function to call
    unsigned nargs;
    unsigned nret;
};

struct MLMthCall
{
    unsigned symidx; // symbol idx of function to call
    unsigned nargs;
    unsigned nret;
};

struct MLFuncDef
{
    unsigned nargs; // consumes this many from the symbol stack
    unsigned nret;  // -"-
    unsigned flags; // TODO: vararg params, vararg return
    unsigned numOps;
    // args[], rets[] follows
};

struct MLConstruct
{
    enum What
    {
        ARRAY,
        TABLE,
    };
    unsigned what;
    unsigned arraySize;
    unsigned hashSizeOrTypeidx; // if table: size of hash part; if array: symbol idx of type
};

struct MLLoadVal
{
    unsigned validx; // index in constant table
};

struct MLOp
{
    unsigned char op;
    // any math op
    // [], tab.idx
    //
};

class MLIRContainer
{
public:
    MLIRContainer(GC& gc);


    GaffaError import(HLNode *root, const StringPool& pool, const char *fn);


    Symstore syms;

private:
    template<typename T> void add(const T& t)
    {
        const size_t N = sizeof(T) / sizeof(unsigned);
        static_assert(N, "oops");
        const unsigned *p = reinterpret_cast<const unsigned*>(&t);
        _add(p, N, T::Cmd);
    }

    enum PrepassResult
    {
        SKIP_CHILDREN     = 0x1,
    };

    void _add(const unsigned *p, const size_t n, unsigned cmd);
    void _processRec(HLNode *n);
    ScopeType _precheckScope(HLNode *n);
    unsigned  _pre(HLNode *n); // -> PrepassResult
    void _post(HLNode *n);
    void _decl(HLNode *n, MLSymbolRefContext ref, unsigned typeidx);
    unsigned _refer(HLNode *n, MLSymbolRefContext ref); // returns symbol index of type, 0 for auto-deduct
    void _codegen(HLNode *n);

    std::vector<unsigned> mops;
    const StringPool *curpool;
    StringPool symbolnames;
    const char *_fn;
};


