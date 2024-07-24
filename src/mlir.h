// Medium-level intemediate representation
// This is intended to be serializable and loadable, with the following considerations:
// - instantiate LLIR by linking MLIR with a symbol table (aka environment)
// - Because we don't necesarily have all the symbols yet, we can't do constant folding
// - But we can figure out which symbols must be types

#pragma once

#include "gainternal.h"
#include "symstore.h"

#include <vector>


struct HLNode;
class StringPool;

enum MLCmd
{
    ML_DECLARE,
    ML_VAL,
    ML_OP,
};

enum MLSymbolRefContext
{
    SYMREF_STANDARD = 0x01,  // Just any symbol (plain old variable, no special properties)
    SYMREF_TYPE     = 0x02,  // Symbol is used as a type
    SYMREF_CALL     = 0x04,  // Symbol is called (-> symbol is function-ish)
    SYMREF_INDEX    = 0x08,  // Symbol is indexed (sym.thing or sym[])
    SYMREF_HASMTH   = 0x10   // Symbol is used in a method call context (sym:method())
};

struct MLExternalSymbol
{
    enum Flags
    {
        IS_TYPE // if set, this symbol is used as a type
    };
    unsigned strid; // var name
    unsigned flags;
};

// When parsing the ML code, we keep a stack of symbols and refer to those via index
// External symbols are at the top and stay constant
// Anything that follows is pushed and popped as scopes change

// General rules:
// - every symbol has a known type
// - if we know the symbol idx, we have a type



struct MLDeclSym // create a symbol
{
    unsigned typeidx; // symbol idx of type
    unsigned nameid;
};

struct MLPopSyms
{
    unsigned n;
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
    MLIRContainer();

    GaffaError import(HLNode *root, const StringPool& pool);


    Symstore syms;

private:
    template<typename T> void _add(const T& t)
    {
        const size_t N = sizeof(T) / sizeof(unsigned);
        const unsigned *p = reinterpret_cast<const unsigned*>(&t);
        _add(p, N);
    }

    void _add(const unsigned *p, const size_t n);
    void _processRec(HLNode *n);
    bool _pre(HLNode *n);
    void _post(HLNode *n);
    void _decl(HLNode *n, MLSymbolRefContext ref);
    void _refer(HLNode *n, MLSymbolRefContext ref);
    void _checkblock(HLNode *n, bool push);

    std::vector<unsigned> mops;
    const StringPool *curpool;
};


