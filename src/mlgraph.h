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
    enum Flags
    {
        EXT_CHILDREN = 0x01,
    };

    union
    {
        struct
        {
            union
            {
                struct
                {
                    MLNode **ptr; // ... This is heap-allocated when we have too many children to fit...
                    u32 num, cap;
                } many;

                // On 32bit platforms this can store 3 pointers, on 64bit platforms only 2
                MLNode *some[sizeof(many) / sizeof(MLNode*)]; // ... but most nodes have <= 2 children, and fit here.

            } ch;

            MLParams p;
        } mid;

        struct
        {
            // This padding is to make asValue() work
            byte _pad[sizeof(mid) - sizeof(_AnyValU)];

            // This memory configuration (_AnyValU directly followed by Type) is the same as ValU.
            _AnyValU val; // type is stored separately below
        } leaf;
    } u;

    Type type;

    u32 line;

    MLNode *typesrc; // where our type derivation originally came from. Used to report type clashes.

    byte cmd; // HLNodeType
    byte tok; // Lexer::TokenType
    byte flags;

    //-----------------------

    enum { ChArraySize = Countof(u.mid.ch.some) };


    MLNode *New(BlockListAllocator& bla, uint32_t nch);

    inline       Val &value()       { return *reinterpret_cast<      Val*>(&u.leaf.val); }
    inline const Val &value() const { return *reinterpret_cast<const Val*>(&u.leaf.val); }


    MLNode **children() { return (flags & EXT_CHILDREN ) ? &u.mid.ch.some[0] : u.mid.ch.many.ptr; }
    size_t numchildren() const;

    MLNode *addChild(MLNode *child, GC& gc);
    void destroy(GC& gc);


    void fold(FoldTracker& ft);
    bool typecheck(TypeTracker& tt);
};
