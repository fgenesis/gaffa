#pragma once

#include "mlir.h"



struct FoldTracker
{
};

struct TypeTracker
{
};

union MLParams
{
    u32 params[2];
    struct { u32 constantId; } loadk;
    struct { u32 op; } op; // unop & binop
    struct { u32 symidx; } refvar; // reflocal, upval, refsym
    struct { u32 symidx; } decl; // tvar, tconst
};

template<typename T> struct MLParamSizeT {};


struct MLNode
{
    MLCmd cmd;
    Type type;
    u32 nch; // if 0, node is a leaf

    u32 line;
    MLNode *typesrc; // where our first type derivation came from. Used to report type clashes.

    union
    {
        union
        {
            ValU val;
        } leaf;

        struct
        {
            union
            {
                MLNode *some[3];
                MLNode **many;
            } ch;

            MLParams p;
        } mid;
    } u;




    MLNode **children() { return nch ? (nch <= Countof(u.mid.ch.some) ? &u.mid.ch.some[0] : u.mid.ch.many) : NULL; }


    void fold(FoldTracker& ft);
    bool typecheck(TypeTracker& tt);
};
