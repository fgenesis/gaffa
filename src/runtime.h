#pragma once

#include "strings.h"
#include "rttypes.h"
#include "gc.h"


// internal use only
/*struct OpRegHelper
{
    Lexer::TokenType tt;
    LeafFunc lfunc;
    const OpDef *def;
    const Type *params;
    const Type *rets;
};*/

struct Runtime
{
    Runtime();
    ~Runtime();
    bool init(Galloc alloc);

    GC gc;
    StringPool sp;
    TypeRegistry tr;

    //void registerOperator(SymTable& syms, const OpRegHelper& reg);
};
