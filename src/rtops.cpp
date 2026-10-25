#include <cmath>
#include <limits.h>
#include <string.h>

#include "rtops.h"
#include "gavm.h"
#include "symtable.h"
#include "runtime.h"
#include "lex.h"
#include "array.h"

template<typename T>
struct TFuncTypes
{
    // Each of these needs a template specialization struct below
    typedef bool (*CompBin)(T a, T b);
    typedef bool (*CompUn)(T a);
    typedef int (*Binary)(T& a, T b);
    typedef int (*Unary)(T& a);
};

template<typename T>
struct TDef {};

template<> struct TDef<uint> : TFuncTypes<uint>
{
    static const PrimType Prim = PRIMTYPE_UINT;
    typedef uint Type;
    FORCEINLINE static uint& Get(ValU& v) { return v.u.ui; }
    FORCEINLINE static void Set(ValU& v, uint x) { v.u.ui = x; }
};

template<> struct TDef<sint> : TFuncTypes<sint>
{
    static const PrimType Prim = PRIMTYPE_SINT;
    typedef sint Type;
    FORCEINLINE static sint& Get(ValU& v) { return v.u.si; }
    FORCEINLINE static void Set(ValU& v, sint x) { v.u.si = x; }
};

template<> struct TDef<real> : TFuncTypes<real>
{
    static const PrimType Prim = PRIMTYPE_FLOAT;
    typedef real Type;
    FORCEINLINE static real& Get(ValU& v) { return v.u.f; }
    FORCEINLINE static void Set(ValU& v, real x) { v.u.f = x; }
};

template<> struct TDef<bool> : TFuncTypes<bool>
{
    static const PrimType Prim = PRIMTYPE_BOOL;
    typedef bool Type;
    FORCEINLINE static uint& Get(ValU& v) { return v.u.ui; }
    FORCEINLINE static void Set(ValU& v, bool x) { v.u.ui = x; }
};

template<> struct TDef<widereal> : TFuncTypes<widereal> // Dummy
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
template<typename T> static FORCEINLINE int Add(T& a, T b) { a += b; return 1; }
template<typename T> static FORCEINLINE int Sub(T& a, T b) { a -= b; return 1; }
template<typename T> static FORCEINLINE int Mul(T& a, T b) { a *= b; return 1; }
                     static FORCEINLINE int FDiv(widereal& a, widereal b) { if(!b) return RTE_DIV_BY_ZERO; a /= b; return 1; }
template<typename T> static FORCEINLINE int Div(T& a, T b) { if(!b) return RTE_DIV_BY_ZERO; a /= b; return 1; }
template<typename T> static FORCEINLINE int Mod(T& a, T b) { if(!b) return RTE_DIV_BY_ZERO; a %= b; return 1; }
template<typename T> static FORCEINLINE int UPos(T& a) { a = +a; return 1; }
template<typename T> static FORCEINLINE int UNeg(T& a) { a = -a; return 1; }

// Integer overflow checks
template<> FORCEINLINE int Add<uint>(uint& a, uint b) { return add_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Sub<uint>(uint& a, uint b) { return sub_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Mul<uint>(uint& a, uint b) { return mul_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Add<sint>(sint& a, sint b) { return add_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Sub<sint>(sint& a, sint b) { return sub_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Mul<sint>(sint& a, sint b) { return mul_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Div<sint>(sint& a, sint b) { if(!b) return RTE_DIV_BY_ZERO; return div_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Mod<sint>(sint& a, sint b) { if(!b) return RTE_DIV_BY_ZERO; a = b == -1 ? 0 : a % b; return 1; } // MIN % -1 is UB in C, but 0
template<> FORCEINLINE int UNeg<sint>(sint& a) { return neg_check_overflow(&a, a) ? RTE_OVERFLOW : 1; }
// Unary + and - on uint result in an int; a holds the int's bits afterwards
template<> FORCEINLINE int UPos<uint>(uint& a) { return a > (uint)IntLimits<sint>::Max() ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int UNeg<uint>(uint& a) { if(a > (uint)IntLimits<sint>::Max() + 1) return RTE_OVERFLOW; a = 0 - a; return 1; }

// Wrapping arithmetic (+% -% *% and prefix -%). Done in unsigned, so wrapping signed ints is well-defined.
template<typename T> struct MakeUnsigned {};
template<> struct MakeUnsigned<uint> { typedef uint Type; };
template<> struct MakeUnsigned<sint> { typedef uint Type; };
static FORCEINLINE uint FromUnsigned(uint u, uint) { return u; }
static FORCEINLINE sint FromUnsigned(uint u, sint) { return u <= (uint)IntLimits<sint>::Max() ? (sint)u : -(sint)~u - 1; }
template<typename T> static FORCEINLINE int AddWrap(T& a, T b) { typedef typename MakeUnsigned<T>::Type U; a = FromUnsigned(U(a) + U(b), a); return 1; }
template<typename T> static FORCEINLINE int SubWrap(T& a, T b) { typedef typename MakeUnsigned<T>::Type U; a = FromUnsigned(U(a) - U(b), a); return 1; }
template<typename T> static FORCEINLINE int MulWrap(T& a, T b) { typedef typename MakeUnsigned<T>::Type U; a = FromUnsigned(U(a) * U(b), a); return 1; }
template<typename T> static FORCEINLINE int UNegWrap(T& a) { typedef typename MakeUnsigned<T>::Type U; a = FromUnsigned(U(0) - U(a), a); return 1; } // uint: a holds the int's bits afterwards

template<typename T> static FORCEINLINE int UNot(T& a) { a = !a; return 1; }

// integer variant of wrapping mod returns T(-1) aka all bits set
//template<typename T> static FORCEINLINE int WrapDiv(real& a, real b) { a = b ? a / b : T(-1); return 1; }
//template<typename T> static FORCEINLINE int WrapWMod(real& a, real b) { a = b ? a % b : T(-1); return 1; }

// NB: On some archs (ARM), shifts by >= register size is UB
template<typename T>
struct Bit
{
    enum { Bits = sizeof(T) * CHAR_BIT, ShMask = Bits - 1};
};

// shif-left overflow is an error
template<typename T> static FORCEINLINE int Shl(T& a, T b) { a = b <= Bit<T>::ShMask ? a << b : 0; return 1; } // TODO: check overflow
template<typename T> static FORCEINLINE int Shr(T& a, T b) { a = b <= Bit<T>::ShMask ? a >> b : 0; return 1; }
template<> FORCEINLINE int Shl<uint>(uint& a, uint b) { return shl_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Shl<sint>(sint& a, sint b) { if(b < 0) return RTE_OVERFLOW; return shl_check_overflow(&a, a, b) ? RTE_OVERFLOW : 1; }
template<> FORCEINLINE int Shr<sint>(sint& a, sint b) { if(b < 0) return RTE_OVERFLOW; a = b <= Bit<sint>::ShMask ? a >> b : (a < 0 ? -1 : 0); return 1; } // arithmetic shift

// rotation is always modulo bitsize
template<typename T> static FORCEINLINE int Rol(T& a, T b) { b &= Bit<T>::ShMask; a = (a << b) | (a >> (Bit<T>::Bits-b)); return 1; }
template<typename T> static FORCEINLINE int Ror(T& a, T b) { b &= Bit<T>::ShMask; a = (a >> b) | (a << (Bit<T>::Bits-b)); return 1; }

template<typename T> static FORCEINLINE int BAnd(T& a, T b) { a &= b; return 1; }
template<typename T> static FORCEINLINE int BOr (T& a, T b) { a |= b; return 1; }
template<typename T> static FORCEINLINE int BXor(T& a, T b) { a ^= b; return 1; }

template<typename T> static FORCEINLINE int UCpl(T& a) { a = ~a; return 1; }

// --- Overloads ---
//static FORCEINLINE int WrapDiv(real& a, real b) { a /= b; return 1; }
//static FORCEINLINE int WrapMod(real& a, real b) { a = std::fmod(a, b); return 1; }
static FORCEINLINE int Mod(real& a, real b) { a = std::fmod(a, b); return 1; }

// This is only evaluated for primitive types;
// describes whether a = a * b is the same as a = b * a,
// which is used for deciding if a = b * a
// can be transformed into a *= b (ie. assignment-combined operator)
template<OperatorId opid> struct IsCommutative : CompileFalse {};
template<> struct IsCommutative<OP_ADD> : CompileTrue {};
template<> struct IsCommutative<OP_MUL> : CompileTrue {};
template<> struct IsCommutative<OP_BITAND> : CompileTrue {};
template<> struct IsCommutative<OP_BITOR> : CompileTrue {};
template<> struct IsCommutative<OP_XOR> : CompileTrue {};
template<> struct IsCommutative<OP_ADDWRAP> : CompileTrue {};
template<> struct IsCommutative<OP_MULWRAP> : CompileTrue {};


static void _Register(OpReg& r, const OpDef *def, OperatorId op, LeafFunc lfunc, const Type *params, size_t nparams, const Type *rets, size_t nrets)
{
    const Type tp = r.tr.mklistDesc(params, nparams)->id();
    const Type tret = r.tr.mklistDesc(rets, nrets)->id();
    const Type tf = r.tr.mkfuncDesc(tp, tret)->id();
    const char *opname = Lexer::GetOperatorName(op);
    assert(opname);
    const sref sname = r.sp.put(opname).id;

    DFunc * const df = DFunc::GCNew(r.gc);
    df->info.paramtype = tp;
    df->info.rettype = tret;
    df->info.functype = tf;
    df->info.nargs = nparams;
    df->info.nrets = nrets;
    df->info.flags = FuncInfo::LFunc | FuncInfo::Pure;
    df->info.nupvals = 0;
    df->u.lfunc = lfunc;
    df->dbg.linestart = 0;
    df->dbg.lineend = 0;
    df->dbg.name = 0; // TODO: C function name
    df->dtype = r.tr.lookupDesc(PRIMTYPE_FUNC)->h.dtype;
    df->opdef = def;

    // The assumtion is that the first parameter's type is also the namespace
    // ie. makes sense in the case of uint.+(uint, uint)
    r.syms.addToNamespace(r.gc, params[0], sname, Val(df));
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

template<typename RT, typename IT, typename ST, typename TDef<IT>::Binary F, OperatorId opid>
struct WrappedBinOp : WrappedBase<RT, IT, ST>
{
    static FORCEINLINE int Doit(Val *dst, Val *a, Val *b)
    {
        IT x = LoadChecked(*a);
        const int e = F(x, LoadChecked(*b));
        StoreChecked(*dst, x);
        return e;
    }

    static VMFUNC_MTH_IMM(OpInplace, Imm_2xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        int e = Doit(d, d, a);
        if(e < 0)
            FAIL(e);
        NEXT();
    }

    static VMFUNC_MTH_IMM(Op, Imm_3xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        Val *b = LOCAL(imm->c);
        int e = Doit(d, a, b);
        if(e < 0)
            FAIL(e);
        NEXT();
    }

    // Fallback function for compile-time evaluation
    // Protocol: Read params from v[0..], write return values to v[0]
    static NOINLINE int Func(VM *vm, Val *v)
    {
        IT x = LoadChecked(v[0]);
        const int e = F(x, LoadChecked(v[1]));
        if(e < 0)
            return e;
        Store(v[0], x);
        return 1;
    }

    static size_t GenOp(void *dst, const u32 *argslots)
    {
        const bool commutative = IsCommutative<opid>::value;
        if(argslots[0] == argslots[1] || (commutative && argslots[0] == argslots[2]))
        {
            Imm_2xu32 imm { argslots[0], argslots[0] == argslots[1] ? argslots[2] : argslots[1] };
            return writeInst(dst, op_OpInplace, imm);
        }

        Imm_3xu32 imm { argslots[0], argslots[1], argslots[2] };
        return writeInst(dst, op_Op, imm);
    }

    static void Register(OpReg& r)
    {
        static const OpDef opdef =
        {
            GenOp, sizeof(Imm_3xu32)
        };

        const Type params[] = { TDef<ST>::Prim, TDef<ST>::Prim };
        const Type rets[] = { TDef<RT>::Prim };
        _Register(r, &opdef, opid, Func, params, Countof(params), rets, Countof(rets));
    }
};

template<typename RT, typename IT, typename ST, typename TDef<IT>::Unary F, OperatorId opid>
struct WrappedUnOp : WrappedBase<RT, IT, ST>
{

    static FORCEINLINE int Doit(Val *dst, Val *a)
    {
        IT x = LoadChecked(*a);
        int e = F(x);
        StoreChecked(*dst, x);
        return e;
    }

    static VMFUNC_MTH_IMM(OpInplace, Imm_2xu32)
    {
        Val *d = LOCAL(imm->a);
        int e = Doit(d, d);
        if(e < 0)
            FAIL(e);
        NEXT();
    }

    static VMFUNC_MTH_IMM(Op, Imm_2xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        int e = Doit(d, a);
        if(e < 0)
            FAIL(e);
        NEXT();
    }

    static int Func(VM *vm, Val *v)
    {
        IT x = LoadChecked(v[0]);
        const int e = F(x);
        if(e < 0)
            return e;
        Store(v[0], x);
        return 1;
    }

    static size_t GenOp(void *dst, const u32 *argslots)
    {
        // float +x is a nop
        if(TDef<ST>::Prim == PRIMTYPE_FLOAT && opid == OP_UPLUS)
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

    static void Register(OpReg& r)
    {
        static const OpDef opdef =
        {
            GenOp, sizeof(Imm_2xu32)
        };
        const Type params[] = { TDef<ST>::Prim };
        const Type rets[] = { TDef<RT>::Prim };
        _Register(r, &opdef, opid, Func, params, Countof(params), rets, Countof(rets));
    }
};


template<typename T, typename TDef<T>::CompBin F, OperatorId opid>
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
    static NOINLINE int Func(VM *vm, Val *v)
    {
        T a = LoadChecked(v[0]);
        T b = LoadChecked(v[1]);
        bool x =  F(a, b);
        Store(v[0], x);
        return 1;
    }

    static size_t GenOp(void *dst, const u32 *argslots)
    {
        Imm_3xu32 imm { argslots[0], argslots[1], argslots[2] };
        return writeInst(dst, op_Op, imm);
    }

    static void Register(OpReg& r)
    {
        static const OpDef opdef =
        {
            GenOp, sizeof(Imm_3xu32)
        };

        const Type params[] = { TDef<T>::Prim, TDef<T>::Prim };
        const Type rets[] = { PRIMTYPE_BOOL };
        // FIXME: These don't ever fail
        _Register(r, &opdef, opid, Func, params, Countof(params), rets, Countof(rets));
    }
};


// a <=> b: int < 0, 0 or > 0
template<typename T>
struct WrappedCmp
{
    static FORCEINLINE int Doit(Val *dst, Val *a, Val *b)
    {
        assert(a->type == TDef<T>::Prim && b->type == TDef<T>::Prim);
        *dst = Val((sint)ValCmp<T>(TDef<T>::Get(*a), TDef<T>::Get(*b)));
        return 1;
    }

    static VMFUNC_MTH_IMM(Op, Imm_3xu32)
    {
        Val *d = LOCAL(imm->a);
        Val *a = LOCAL(imm->b);
        Val *b = LOCAL(imm->c);
        Doit(d, a, b);
        NEXT();
    }

    // Fallback function for compile-time evaluation
    static NOINLINE int Func(VM *vm, Val *v)
    {
        return Doit(&v[0], &v[0], &v[1]);
    }

    static size_t GenOp(void *dst, const u32 *argslots)
    {
        Imm_3xu32 imm { argslots[0], argslots[1], argslots[2] };
        return writeInst(dst, op_Op, imm);
    }

    static void Register(OpReg& r)
    {
        static const OpDef opdef =
        {
            GenOp, sizeof(Imm_3xu32)
        };

        const Type params[] = { TDef<T>::Prim, TDef<T>::Prim };
        const Type rets[] = { PRIMTYPE_SINT };
        _Register(r, &opdef, OP_CMP, Func, params, Countof(params), rets, Countof(rets));
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
    typedef WrappedBinComp<T, C_Eq, OP_EQ> _Eq;

    static void Register(OpReg& r)
    {
        _Eq::Register(r);
    }
};

template<typename T>
struct Arithmetic
{
    typedef typename MakeSigned<T>::Type S;
    typedef typename MakeInt<T>::Type I;
    typedef WrappedBinOp<T, T, T, Add<T>, OP_ADD> _Add;
    typedef WrappedBinOp<T, T, T, Sub<T>, OP_SUB> _Sub;
    typedef WrappedBinOp<T, T, T, Mul<T>, OP_MUL> _Mul;
    typedef WrappedBinOp<real, widereal, T, FDiv, OP_FDIV> _FDiv;
    typedef WrappedBinOp<T, T, T, Div, OP_IDIV> _Div;
    typedef WrappedBinOp<T, T, T, Mod, OP_MOD> _Mod;
    // Unary + or minus always converts to signed
    typedef WrappedUnOp<S, T, T, UPos<T>, OP_UPLUS> _UPlus;
    typedef WrappedUnOp<S, T, T, UNeg<T>, OP_UNEG> _UNeg;

    static void Register(OpReg& r)
    {
        _Add::Register(r);
        _Sub::Register(r);
        _Mul::Register(r);
        _FDiv::Register(r);
        _Div::Register(r);
        _Mod::Register(r);
        _UPlus::Register(r);
        _UNeg::Register(r);
    }
};

// Integers only
template<typename T>
struct WrappingArithmetic
{
    typedef typename MakeSigned<T>::Type S;
    typedef WrappedBinOp<T, T, T, AddWrap<T>, OP_ADDWRAP> _AddWrap;
    typedef WrappedBinOp<T, T, T, SubWrap<T>, OP_SUBWRAP> _SubWrap;
    typedef WrappedBinOp<T, T, T, MulWrap<T>, OP_MULWRAP> _MulWrap;
    typedef WrappedUnOp<S, T, T, UNegWrap<T>, OP_UNEGWRAP> _UNegWrap; // like unary -, converts to signed

    static void Register(OpReg& r)
    {
        _AddWrap::Register(r);
        _SubWrap::Register(r);
        _MulWrap::Register(r);
        _UNegWrap::Register(r);
    }
};

template<typename T>
struct Logical
{
    typedef WrappedUnOp<T, T, T, UNot<T>, OP_UNOT> _UNot;

    static void Register(OpReg& r)
    {
        _UNot::Register(r);
    }
};

// < <= > >= are derived from <=> during folding and codegen, they can't be registered
template<typename T>
struct Orderable
{
    static void Register(OpReg& r)
    {
        WrappedCmp<T>::Register(r);
    }
};

template<typename T>
struct Bitwise
{
    typedef WrappedBinOp<T, T, T, BAnd<T>, OP_BITAND> _BAnd;
    typedef WrappedBinOp<T, T, T, BOr<T>, OP_BITOR> _BOr;
    typedef WrappedBinOp<T, T, T, BXor<T>, OP_XOR> _BXor;
    typedef WrappedBinOp<T, T, T, Shl<T>, OP_SHL> _Shl;
    typedef WrappedBinOp<T, T, T, Shr<T>, OP_SHR> _Shr;
    typedef WrappedUnOp<T, T, T, UCpl<T>, OP_UBITNOT> _UCpl;


    static void Register(OpReg& r)
    {
        _BAnd::Register(r);
        _BOr::Register(r);
        _BXor::Register(r);
        _Shl::Register(r);
        _Shr::Register(r);
        _UCpl::Register(r);
    }
};

template<typename T>
static void reg_intT(OpReg& r)
{
    Equality<T>::Register(r);
    Orderable<T>::Register(r);
    Arithmetic<T>::Register(r);
    WrappingArithmetic<T>::Register(r);
    Bitwise<T>::Register(r);
}

void reg_float_ops(OpReg& r)
{
    Equality<real>::Register(r);
    Orderable<real>::Register(r);
    Arithmetic<real>::Register(r);
}

void reg_bool_ops(OpReg& r)
{
    Equality<bool>::Register(r);
    Logical<bool>::Register(r);
}

// Strings are interned, so == compares ids. <=> compares bytes, shorter first if one is a prefix of the other.
static int str_eq(VM *vm, Val *v)
{
    assert(v[0].type == PRIMTYPE_STRING && v[1].type == PRIMTYPE_STRING);
    v[0] = Val(v[0].u.str == v[1].u.str);
    return 1;
}

static int str_cmp(VM *vm, Val *v)
{
    assert(v[0].type == PRIMTYPE_STRING && v[1].type == PRIMTYPE_STRING);
    const Strp a = vm->rt->sp.lookup(v[0].u.str);
    const Strp b = vm->rt->sp.lookup(v[1].u.str);
    int c = memcmp(a.s, b.s, a.len < b.len ? a.len : b.len);
    if(!c)
        c = ValCmp(a.len, b.len);
    v[0] = Val((sint)Sign(c));
    return 1;
}

static int str_len(VM *vm, Val *v)
{
    assert(v[0].type == PRIMTYPE_STRING);
    v[0] = Val((uint)vm->rt->sp.lookup(v[0].u.str).len);
    return 1;
}

static int str_concat(VM *vm, Val *v)
{
    assert(v[0].type == PRIMTYPE_STRING && v[1].type == PRIMTYPE_STRING);
    StringPool& sp = vm->rt->sp;
    const Strp a = sp.lookup(v[0].u.str);
    const Strp b = sp.lookup(v[1].u.str);
    if(!b.len)
        return 1;
    PodArray<char> buf;
    char *p = buf.alloc_n(vm->rt->gc, a.len + b.len);
    if(!p)
        return RTE_ALLOC_FAIL;
    memcpy(p, a.s, a.len);
    memcpy(p + a.len, b.s, b.len);
    const Str s = sp.put(p, a.len + b.len);
    buf.dealloc(vm->rt->gc);
    v[0] = Val(s);
    return 1;
}

void reg_string_ops(OpReg& r)
{
    const Type str1[] = { PRIMTYPE_STRING };
    const Type str2[] = { PRIMTYPE_STRING, PRIMTYPE_STRING };
    const Type rbool[] = { PRIMTYPE_BOOL };
    const Type rsint[] = { PRIMTYPE_SINT };
    const Type ruint[] = { PRIMTYPE_UINT };
    _Register(r, NULL, OP_EQ, str_eq, str2, Countof(str2), rbool, Countof(rbool));
    _Register(r, NULL, OP_CMP, str_cmp, str2, Countof(str2), rsint, Countof(rsint));
    _Register(r, NULL, OP_ULEN, str_len, str1, Countof(str1), ruint, Countof(ruint));
    _Register(r, NULL, OP_CONCAT, str_concat, str2, Countof(str2), str1, Countof(str1));
}

void reg_uint_ops(OpReg& r)
{
    reg_intT<uint>(r);
}

void reg_sint_ops(OpReg& r)
{
    reg_intT<sint>(r);
}
