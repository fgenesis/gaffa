#include "mlgraph.h"


MLNode *MLNode::New(BlockListAllocator& bla, uint32_t nch)
{
    MLNode *n = (MLNode*)bla.alloc(sizeof(MLNode));
    // n is already cleared to 0
    if(nch > ChArraySize)
    {
        n->u.mid.ch.many.cap = nch;
        n->u.mid.ch.many.num = nch;
        n->flags = EXT_CHILDREN;
        n->u.mid.ch.many.ptr = gc_alloc_unmanaged_T<MLNode*>(bla.gc, NULL, 0, nch); // FIXME: handle alloc fail
    }
    return n;
}


/*
Folding rules:
- variables are replaced with constants if their values are known
- any operator/function/method that is known at compile time because types are known can be inlined
  (instead of looking it up at runtime)
- (constant op constant) can be calculated and replaced with a constant
- functions known to be compile-time executable can be replaced with a value when all params are constant values
- implicit casts are fine where allowed (x+1 where x is a known sint; 1 is uint)
*/

size_t MLNode::numchildren() const
{
    if(flags & EXT_CHILDREN)
        return u.mid.ch.many.num;

    u32 i = 0;
    for(; i < Countof(u.mid.ch.some); ++i)
        if(!u.mid.ch.some[i])
            break;
    return i;
}

MLNode* MLNode::addChild(MLNode* const child, GC& gc)
{
    size_t cap;
    MLNode **oldmem, **newmem;
    u32 n;
    if(!(flags & EXT_CHILDREN))
    {
        for(u32 i = 0; i < Countof(u.mid.ch.some); ++i)
            if(!u.mid.ch.some[i])
            {
                u.mid.ch.some[i] = child;
                return;
            }
        // No more space in fixed array, need to allocate
        cap = 0;
        oldmem = NULL;
    }
    else
    {
        cap = u.mid.ch.many.cap;
        n = u.mid.ch.many.num;
        newmem = u.mid.ch.many.ptr;
        if(n < cap)
            goto put;
        oldmem = newmem;
    }

    {
        const size_t newcap = 6 + (2 * cap);
        newmem = gc_alloc_unmanaged_T<MLNode*>(gc, oldmem, cap, newcap);
        if(!newmem)
            return NULL;

        u.mid.ch.many.ptr = newmem;
        u.mid.ch.many.cap = newcap;
        flags |= EXT_CHILDREN;
    }

    n = u.mid.ch.many.num;
put:
    newmem[n] = child;
    u.mid.ch.many.num = n + 1;

    return child;
}

void MLNode::destroy(GC& gc)
{
    if(flags & EXT_CHILDREN)
        gc_alloc_unmanaged_T(gc, u.mid.ch.many.ptr, u.mid.ch.many.cap, 0);
}

template<typename T>
struct FoldOpsBase
{
    enum { Bitsize = sizeof(T) * CHAR_BIT };

    /*typedef bool (*Binary)(T& r, T a, T b);
    typedef bool (*Unary)(T& r, T a);
    typedef bool (*Compare)(T a, T& b);*/

    static bool Add(T& r, T a, T b) { r = a + b; return true; }
    static bool Sub(T& r, T a, T b) { r = a - b; return true; }
    static bool Mul(T& r, T a, T b) { r = a * b; return true; }
    static bool Div(T& r, T a, T b) { if(!b) return false; r = a / b; return true; }
    static bool Mod(T& r, T a, T b) { if(!b) return false; r = a % b; return true; }
    static bool Shl(T& r, T a, T b) { if(b >= Bitsize) return false; r = a << b; return true; }
    static bool Shr(T& r, T a, T b) { if(b >= Bitsize) return false; r = a >> b; return true; }
    static bool Rol(T& r, T a, T b) { if(b >= Bitsize) return false; r = (a << b) | (a >> (Bitsize-b)); return true; }
    static bool Ror(T& r, T a, T b) { if(b >= Bitsize) return false; r = (a >> b) | (a << (Bitsize-b)); return true; }

    static bool BAnd(T& r, T a, T b) { r = a & b; return true; }
    static bool BOr (T& r, T a, T b) { r = a | b; return true; }
    static bool BXor(T& r, T a, T b) { r = a ^ b; return true; }

    static bool UPos(T& r, T a) { r = +a; return true; }
    static bool UNeg(T& r, T a) { r = -a; return true; }
    static bool UNot(T& r, T a) { r = !a; return true; }
    static bool UCpl(T& r, T a) { r = ~a; return true; }

    // Alternative representations of relations so that only < and == are enough to handle everything
    static bool C_Eq (T a, T b) { return  (a == b); }
    static bool C_Neq(T a, T b) { return !(a == b); }
    static bool C_Lt (T a, T b) { return  (a <  b); }
    static bool C_Gt (T a, T b) { return  (b <  a); }
    static bool C_Lte(T a, T b) { return !(b <  a); }
    static bool C_Gte(T a, T b) { return !(a <  b); }
};

template<typename T>
struct FoldOps : FoldOpsBase<T>
{
};

template<>
struct FoldOps<real> : FoldOpsBase<real>
{
    static bool Rol(real& r, real a, real b) { return false; }
    static bool Ror(real& r, real a, real b) { return false; }
    static bool BAnd(real& r, real a, real b) { return false; }
    static bool BOr (real& r, real a, real b) { return false; }
    static bool BXor(real& r, real a, real b) { return false; }

    static bool UCpl(real& r, real a) { return false; }
};


/*
*     static bool Fold(Val& r, T a, T b, BinOpType op)
    {
        switch(op)
        {
#define BINOP(Func) { T t; bool ok = Func(t, a, b); r = t; return ok; }
            case OP_ADD: BINOP(Add);
            case OP_SUB: BINOP(Add);
            case OP_MUL: BINOP(Add);
            case OP_DIV: BINOP(Add);
            case OP_MOD: BINOP(Add);
            case OP_SHL: BINOP(Add);
            case OP_SHR: BINOP(Add);
        }
    }*/

void MLNode::fold(FoldTracker& ft)
{
}

bool MLNode::typecheck(TypeTracker& tt)
{
    return false;
}
