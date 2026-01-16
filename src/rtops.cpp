#include <cmath>

#include "rtops.h"
#include "gavm.h"
#include "symtable.h"
#include "runtime.h"

template<typename T>
struct FuncTypes
{
    // Each of these needs a template specialization struct below
    typedef bool (*CompBin)(T a, T b);
    typedef bool (*CompUn)(T a);
    typedef RTError (*Binary)(T& a, T b);
    typedef RTError (*Unary)(T& a);
};

template<typename T>
struct TDef {};

template<> struct TDef<uint> : FuncTypes<uint>
{
    static const PrimType Prim = PRIMTYPE_UINT;
    typedef uint Type;
    FORCEINLINE static uint& Get(ValU& v) { return v.u.ui; }
    FORCEINLINE static void Set(ValU& v, uint x) { v.u.ui = x; }
};

template<> struct TDef<sint> : FuncTypes<sint>
{
    static const PrimType Prim = PRIMTYPE_SINT;
    typedef sint Type;
    FORCEINLINE static sint& Get(ValU& v) { return v.u.si; }
    FORCEINLINE static void Set(ValU& v, sint x) { v.u.si = x; }
};

template<> struct TDef<real> : FuncTypes<real>
{
    static const PrimType Prim = PRIMTYPE_FLOAT;
    typedef real Type;
    FORCEINLINE static real& Get(ValU& v) { return v.u.f; }
    FORCEINLINE static void Set(ValU& v, real x) { v.u.f = x; }
};

template<> struct TDef<bool> : FuncTypes<bool>
{
    static const PrimType Prim = PRIMTYPE_BOOL;
    typedef bool Type;
    FORCEINLINE static uint& Get(ValU& v) { return v.u.ui; }
    FORCEINLINE static uint& Set(ValU& v, bool x) { v.u.ui = x; }
};

template<> struct TDef<widereal> : FuncTypes<widereal> // Dummy
{
    typedef uint Type;
};

template<typename R, typename T>
struct Cast
{
    static FORCEINLINE T Do(VM *vm, T x) { return x; }

};

// TODO: Cast specializations to check value ranges and error if bad

template<typename R, typename T>
static FORCEINLINE R trycast(VM *vm, T x)
{
    return Cast<R, T>::Do(vm, x);
}


template<typename T> static FORCEINLINE bool C_Eq (T a, T b) { return a == b; }
template<typename T> static FORCEINLINE bool C_Neq(T a, T b) { return a != b; }

template<typename T> static FORCEINLINE bool C_UNot(T& r, T a) { return !a; }
// ||, && are handled during codegen because the RHS is not always evaluated
// and these operators need to be implemented with jumps

// Alternative representations of relations so that only < is needed for comparisons
template<typename T> static FORCEINLINE bool C_Lt (T a, T b) { return  (a <  b); }
template<typename T> static FORCEINLINE bool C_Gt (T a, T b) { return  (b <  a); }
template<typename T> static FORCEINLINE bool C_Lte(T a, T b) { return !(b <  a); }
template<typename T> static FORCEINLINE bool C_Gte(T a, T b) { return !(a <  b); }
template<typename T> static FORCEINLINE RTError Add(T& a, T b) { a += b; return RTE_OK; }
template<typename T> static FORCEINLINE RTError Sub(T& a, T b) { a -= b; return RTE_OK; }
template<typename T> static FORCEINLINE RTError Mul(T& a, T b) { a *= b; return RTE_OK; }
                     static FORCEINLINE RTError FDiv(widereal& a, widereal b) { if(!b) return RTE_DIV_BY_ZERO; a /= b; return RTE_OK; }
template<typename T> static FORCEINLINE RTError Div(T& a, T b) { if(!b) return RTE_DIV_BY_ZERO; a /= b; return RTE_OK; }
template<typename T> static FORCEINLINE RTError Mod(T& a, T b) { if(!b) return RTE_DIV_BY_ZERO; a %= b; return RTE_OK; }
template<typename T> static FORCEINLINE RTError UPos(T& a) { a = +a; return RTE_OK; }
template<typename T> static FORCEINLINE RTError UNeg(T& a) { a = -a; return RTE_OK; } // TODO: overflow check

// integer variant of wrapping mod returns T(-1) aka all bits set
//template<typename T> static FORCEINLINE RTError WrapDiv(real& a, real b) { a = b ? a / b : T(-1); return RTE_OK; }
//template<typename T> static FORCEINLINE RTError WrapWMod(real& a, real b) { a = b ? a % b : T(-1); return RTE_OK; }

// NB: On some archs (ARM), shifts by >= register size is UB
template<typename T>
struct Bit
{
    enum { Bits = sizeof(T) * CHAR_BIT, ShMask = Bits - 1};
};

// shif-left overflow is an error
template<typename T> static FORCEINLINE RTError Shl(T& a, T b) { a = b <= Bit<T>::ShMask ? a << b : 0; return RTE_OK; } // TODO: check overflow
template<typename T> static FORCEINLINE RTError Shr(T& a, T b) { a = b <= Bit<T>::ShMask ? a >> b : 0; return RTE_OK; }

// rotation is always modulo bitsize
template<typename T> static FORCEINLINE RTError Rol(T& a, T b) { b &= Bit<T>::ShMask; a = (a << b) | (a >> (Bit<T>::Bits-b)); return RTE_OK; }
template<typename T> static FORCEINLINE RTError Ror(T& a, T b) { b &= Bit<T>::ShMask; a = (a >> b) | (a << (Bit<T>::Bits-b)); return RTE_OK; }

template<typename T> static FORCEINLINE RTError BAnd(T& a, T b) { a &= b; return RTE_OK; }
template<typename T> static FORCEINLINE RTError BOr (T& a, T b) { a |= b; return RTE_OK; }
template<typename T> static FORCEINLINE RTError BXor(T& a, T b) { a ^= b; return RTE_OK; }

template<typename T> static FORCEINLINE RTError UCpl(T& a) { a = ~a; return RTE_OK; }

// --- Overloads ---
//static FORCEINLINE RTError WrapDiv(real& a, real b) { a /= b; return RTE_OK; }
//static FORCEINLINE RTError WrapMod(real& a, real b) { a = std::fmod(a, b); return RTE_OK; }
static FORCEINLINE RTError Mod(real& a, real b) { a = std::fmod(a, b); return RTE_OK; }

// This is only evaluated for primitive types;
// describes whether a = a * b is the same as a = b * a,
// which is used for deciding if a = b * a
// can be transformed into a *= b (ie. assignment-combined operator)
template<Lexer::TokenType tt> struct IsCommutative : CompileFalse {};
template<> struct IsCommutative<Lexer::TOK_PLUS> : CompileTrue {};
template<> struct IsCommutative<Lexer::TOK_STAR> : CompileTrue {};
template<> struct IsCommutative<Lexer::TOK_BITAND> : CompileTrue {};
template<> struct IsCommutative<Lexer::TOK_BITOR> : CompileTrue {};
template<> struct IsCommutative<Lexer::TOK_HAT> : CompileTrue {};


static void _Register(SymTable& syms, Runtime& rt, const OpDef *def, Lexer::TokenType tt, LeafFunc lfunc, const Type *params, size_t nparams, const Type *rets, size_t nrets)
{
    const Type tp = rt.tr.mklist(params, nparams);
    const Type tret = rt.tr.mklist(rets, nrets);
    const Type tf = rt.tr.mkfunc(tp, tret);
    const Lexer::OpName opname = Lexer::GetOperatorName(tt, nparams == 1);
    const sref sname = rt.sp.put(opname.name).id;

    DFunc * const df = DFunc::GCNew(rt.gc);
    df->info.paramtype = tp;
    df->info.rettype = tret;
    df->info.functype = tf;
    df->info.nargs = nparams;
    df->info.nrets = nrets;
    df->info.flags = FuncInfo::LFunc | FuncInfo::Pure;
    df->info.nlocals = 0;
    df->info.nupvals = 0;
    df->u.lfunc = lfunc;
    df->dbg = NULL;
    df->dtype = rt.tr.lookupDesc(PRIMTYPE_FUNC)->h.dtype;
    df->opdef = def;

    // The assumtion is that the first parameter's type is also the namespace
    // ie. makes sense in the case of uint.+(uint, uint)
    syms.addToNamespace(rt.gc, params[0], sname, Val(df));
}

// Some common crap moved out of the way and generalized
template<typename RT, typename IT, typename ST>
struct WrappedBase
{
    static FORCEINLINE IT Load(Val& v) { return (IT)TDef<ST>::Get(v); }
    static FORCEINLINE void Store(Val& v, IT x) { v = Val((RT)x); }

    static FORCEINLINE void StoreChecked(Val &dst, IT x)
    {
        Store(dst, x);
        assert(dst.type == TDef<RT>::Prim);
    }

    static FORCEINLINE IT LoadChecked(Val &src)
    {
        assert(src.type == TDef<ST>::Prim);
        return Load(src);
    }
};

template<typename RT, typename IT, typename ST, typename TDef<IT>::Binary F, Lexer::TokenType tt>
struct WrappedBinOp : WrappedBase<RT, IT, ST>
{
    static FORCEINLINE RTError Doit(Val *dst, Val *a, Val *b)
    {
        IT x = LoadChecked(*a);
        const RTError e = F(x, LoadChecked(*b));
        StoreChecked(*dst, x);
        return e;
    }

    static VMFUNC_MTH_IMM(OpInplace, Imm_2xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        if(RTError e = Doit(d, d, a))
            FAIL(e);
        NEXT();
    }

    static VMFUNC_MTH_IMM(Op, Imm_3xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        Val *b = LOCAL(imm->c);
        if(RTError e = Doit(d, a, b))
            FAIL(e);
        NEXT();
    }

    // Fallback function for compile-time evaluation
    // Protocol: Read params from v[0..], write return values to v[0..]
    static NOINLINE RTError Func(VM *vm, Val *v)
    {
        IT x = LoadChecked(v[0]);
        vm->err = F(x, LoadChecked(v[1]));
        Store(v[0], x);
        assert(vm->err || v[0].type == TDef<RT>::Prim);
        return RTE_OK;
    }

    static size_t GenOp(void *dst, const u32 *argslots)
    {
        const bool commutative = IsCommutative<tt>::value;
        if(argslots[0] == argslots[1] || (commutative && argslots[0] == argslots[2]))
        {
            Imm_2xu32 imm { argslots[0], argslots[0] == argslots[1] ? argslots[2] : argslots[1] };
            return writeInst(dst, op_OpInplace, imm);
        }

        Imm_3xu32 imm { argslots[0], argslots[1], argslots[2] };
        return writeInst(dst, op_Op, imm);
    }

    static void Register(SymTable& syms, Runtime& rt)
    {
        static const OpDef opdef =
        {
            GenOp, sizeof(Imm_3xu32)
        };

        const Type params[] = { TDef<ST>::Prim, TDef<ST>::Prim };
        const Type rets[] = { TDef<RT>::Prim };
        _Register(syms, rt, &opdef, tt, Func, params, Countof(params), rets, Countof(rets));
    }
};

template<typename RT, typename IT, typename ST, typename TDef<IT>::Unary F, Lexer::TokenType tt>
struct WrappedUnOp : WrappedBase<RT, IT, ST>
{

    static FORCEINLINE RTError Doit(Val *dst, Val *a)
    {
        IT x = LoadChecked(*a);
        RTError e = F(x);
        StoreChecked(*dst, x);
        return e;
    }

    static VMFUNC_MTH_IMM(OpInplace, Imm_2xu32)
    {
        Val *d = LOCAL(imm->a);
        if(RTError e = Doit(d, d))
            FAIL(e);
        NEXT();
    }

    static VMFUNC_MTH_IMM(Op, Imm_2xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        if(RTError e = Doit(d, a))
            FAIL(e);
        NEXT();
    }

    static RTError Func(VM *vm, Val *v)
    {
        IT x = LoadChecked(v[0]);
        vm->err = F(x);
        Store(v[0], x);
        assert(vm->err || v[0].type == TDef<RT>::Prim);
        return RTE_OK;
    }

    static size_t GenOp(void *dst, const u32 *argslots)
    {
        // float +x is a nop
        if(TDef<ST>::Prim == PRIMTYPE_FLOAT && tt == Lexer::TOK_PLUS)
        {
            return 0;
        }
        if(argslots[0] == argslots[1])
        {
            Imm_u32 imm { argslots[0] };
            return writeInst(dst, op_OpInplace, imm);
        }

        Imm_2xu32 imm { argslots[0], argslots[1] };
        return writeInst(dst, op_Op, imm);
    }

    static void Register(SymTable& syms, Runtime& rt)
    {
        static const OpDef opdef =
        {
            GenOp, sizeof(Imm_2xu32)
        };
        const Type params[] = { TDef<ST>::Prim };
        const Type rets[] = { TDef<RT>::Prim };
        _Register(syms, rt, &opdef, tt, Func, params, Countof(params), rets, Countof(rets));
    }
};


template<typename T, typename TDef<T>::CompBin F, Lexer::TokenType tt>
struct WrappedBinComp : WrappedBase<bool, bool, T>
{
    static VMFUNC_MTH_IMM(Op, Imm_3xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        Val *b = LOCAL(imm->c);
        bool x = F(LoadChecked(*a), LoadChecked(*b));
        StoreChecked(*d, x);
        NEXT();
    }

    // Fallback function for compile-time evaluation
    // Protocol: Read params from v[0..], write return values to v[0..]
    static NOINLINE RTError Func(VM *vm, Val *v)
    {
        T a = LoadChecked(v[0]);
        T b = LoadChecked(v[1]);
        bool x =  F(a, b);
        Store(v[0], x);
        return RTE_OK;
    }

    static size_t GenOp(void *dst, const u32 *argslots)
    {
        Imm_3xu32 imm { argslots[0], argslots[1], argslots[2] };
        return writeInst(dst, op_Op, imm);
    }

    static void Register(SymTable& syms, Runtime& rt)
    {
        static const OpDef opdef =
        {
            GenOp, sizeof(Imm_3xu32)
        };

        const Type params[] = { TDef<T>::Prim, TDef<T>::Prim };
        const Type rets[] = { PRIMTYPE_BOOL };
        // FIXME: These don't ever fail
        _Register(syms, rt, &opdef, tt, Func, params, Countof(params), rets, Countof(rets));
    }
};


template<typename T> struct MakeSigned {};
template<> struct MakeSigned<uint> { typedef sint Type; };
template<> struct MakeSigned<real> { typedef real Type; };
template<> struct MakeSigned<sint> { typedef sint Type; };

template<typename T> struct MakeInt {};
template<> struct MakeInt<uint> { typedef uint Type; };
template<> struct MakeInt<real> { typedef sint Type; };
template<> struct MakeInt<sint> { typedef sint Type; };

template<typename T>
struct Equality
{
    typedef WrappedBinComp<T, C_Eq, Lexer::TOK_EQ> _Eq;
    typedef WrappedBinComp<T, C_Neq, Lexer::TOK_NEQ> _Neq;

    static void Register(SymTable& syms, Runtime& rt)
    {
        _Eq::Register(syms, rt);
        _Neq::Register(syms, rt);
    }
};

template<typename T>
struct Arithmetic
{
    typedef typename MakeSigned<T>::Type S;
    typedef typename MakeInt<T>::Type I;
    typedef WrappedBinOp<T, T, T, Add<T>, Lexer::TOK_PLUS> _Add;
    typedef WrappedBinOp<T, T, T, Sub<T>, Lexer::TOK_MINUS> _Sub;
    typedef WrappedBinOp<T, T, T, Mul<T>, Lexer::TOK_STAR> _Mul;
    typedef WrappedBinOp<real, widereal, T, FDiv, Lexer::TOK_SLASH> _FDiv;
    typedef WrappedBinOp<T, T, T, Div, Lexer::TOK_SLASH2X> _Div;
    typedef WrappedBinOp<T, T, T, Mod, Lexer::TOK_PERC> _Mod;
    // Unary + or minus always converts to signed
    typedef WrappedUnOp<S, S, T, UPos<S>, Lexer::TOK_PLUS> _UPlus;
    typedef WrappedUnOp<S, S, T, UNeg<S>, Lexer::TOK_MINUS> _UNeg;

    static void Register(SymTable& syms, Runtime& rt)
    {
        _Add::Register(syms, rt);
        _Sub::Register(syms, rt);
        _Mul::Register(syms, rt);
        _FDiv::Register(syms, rt);
        _Div::Register(syms, rt);
        _Mod::Register(syms, rt);
        _UPlus::Register(syms, rt);
        _UNeg::Register(syms, rt);
    }
};

template<typename T>
struct Logical
{

};

template<typename T>
struct Orderable
{

};

template<typename T>
struct Bitwise
{
    typedef WrappedBinOp<T, T, T, BAnd<T>, Lexer::TOK_PLUS> _BAnd;
    typedef WrappedBinOp<T, T, T, BOr<T>, Lexer::TOK_MINUS> _BOr;
    typedef WrappedBinOp<T, T, T, BXor<T>, Lexer::TOK_STAR> _BXor;
    typedef WrappedBinOp<T, T, T, Shl<T>, Lexer::TOK_SHL> _Shl;
    typedef WrappedBinOp<T, T, T, Shr<T>, Lexer::TOK_SHR> _Shr;
    typedef WrappedUnOp<T, T, T, UCpl<T>, Lexer::TOK_SHR> _UCpl;


    static void Register(SymTable& syms, Runtime& rt)
    {
        _BAnd::Register(syms, rt);
        _BOr::Register(syms, rt);
        _BXor::Register(syms, rt);
        _Shl::Register(syms, rt);
        _Shr::Register(syms, rt);
        _UCpl::Register(syms, rt);
    }
};

template<typename T>
static void reg_intT(SymTable& syms, Runtime& rt)
{
    Equality<T>::Register(syms, rt);
    Arithmetic<T>::Register(syms, rt);
    Bitwise<T>::Register(syms, rt);
}

void reg_float_ops(SymTable& syms, Runtime& rt)
{
    Equality<real>::Register(syms, rt);
    Arithmetic<real>::Register(syms, rt);
}

void reg_bool_ops(SymTable& syms, Runtime& rt)
{
    Equality<bool>::Register(syms, rt);
}

void reg_uint_ops(SymTable& syms, Runtime& rt)
{
    reg_intT<uint>(syms, rt);
}

void reg_sint_ops(SymTable& syms, Runtime& rt)
{
    reg_intT<sint>(syms, rt);
}
