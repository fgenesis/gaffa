#include "rttypes.h"
#include "typing.h"
#include "gaobj.h"
#include "symtable.h"
#include "strings.h"
#include "gavm.h"
#include "runtime.h"
#include <assert.h>
#include <limits>


struct RTReg;

struct ClassReg
{
    ClassReg(RTReg& r, DType *cls) : r(r), cls(cls) {}

    template<typename T>
    void symbol(const char *name, const T& obj)
    {
        sref s = r.sp.put(name).id;
        r.syms.addToNamespace(r.gc, cls->tid, s, Val(obj));
    }

    template<size_t NP, size_t NR>
    inline DFunc *method(const char *name, LeafFunc lfunc, const Type (&params)[NP], const Type (&rets)[NR], FuncInfo::Flags extraflags = FuncInfo::None)
    {
        return this->method(name, lfunc, params, NP, rets, NR, extraflags);
    }

    DFunc *method(const char * name, LeafFunc lfunc, const Type * params, size_t nparams, const Type * rets, size_t nrets, FuncInfo::Flags extraflags);
    DFunc *op(Lexer::TokenType optok, LeafFunc lfunc, Type t, unsigned arity);

private:
    RTReg& r;
    DType *cls;
};


struct RTReg
{
    GC& gc;
    StringPool& sp;
    TypeRegistry& tr;
    SymTable& syms;

    struct
    {
        DType *type;
        DType *func;
        DType *sint;
        DType *uint;
        DType *flt;
        DType *str;
        DType *array;
        DType *table;
        DType *symtab;
    } types;

    struct
    {
        Type empty;
    } tids;

    sref str(const char *s)
    {
       return sp.put(s).id;
    }

    template<typename T>
    inline void symbol(const char *name, const T& obj)
    {
        syms.addSymbol(gc, str(name), Val(obj));
    }

    ClassReg regclass(const char *name, DType *t)
    {
        symbol(name, t);
        return ClassReg(*this, t);
    }


    DFunc *regfunc(DType *ns, const char *name, LeafFunc lfunc, const Type *params, size_t nparams, const Type *rets, size_t nrets, FuncInfo::Flags extraflags)
    {
        Type tp = tr.mklist(params, nparams);
        Type tret = tr.mklist(rets, nrets);
        Type tf = tr.mkfunc(tp, tret);
        sref sname = str(name);

        DFunc *df = mkfunc(lfunc, tf, nparams, nrets, extraflags);
        if(ns)
            syms.addToNamespace(gc, ns->tid, sname, Val(df));
        else
            syms.addSymbol(gc, sname, Val(df));

        return df;
    }


    template<size_t NP, size_t NR>
    inline DFunc *regfunc(DType *ns, const char *name, LeafFunc lfunc, const Type (&params)[NP], const Type (&rets)[NR], FuncInfo::Flags extraflags = FuncInfo::None)
    {
        return regfunc(ns, name, lfunc, params, NP, rets, NR, extraflags);
    }

    DFunc *mkfunc(LeafFunc lfunc, Type ty, size_t nargs, size_t nrets, FuncInfo::Flags extraflags)
    {
        DFunc *f = DFunc::GCNew(gc);
        f->info.t = ty;
        f->info.nargs = nargs;
        f->info.nrets = nrets;
        f->info.flags = FuncInfo::LFunc | extraflags;
        f->info.nlocals = 0;
        f->info.nupvals = 0;
        f->u.lfunc = lfunc;
        f->dbg = NULL;
        f->dtype = types.func;
        return f;
    }
};


DFunc *ClassReg::method(const char * name, LeafFunc lfunc, const Type * params, size_t nparams, const Type * rets, size_t nrets, FuncInfo::Flags extraflags)
{
    return r.regfunc(cls, name, lfunc, params, nparams, rets, nrets, extraflags);
}

DFunc *ClassReg::op(Lexer::TokenType optok, LeafFunc lfunc, Type t, unsigned arity)
{
    assert(arity <= 2);
    const char *name = Lexer::GetTokenText(optok);
    Type ta[] = { t, t };
    return this->method(name, lfunc, ta, arity, ta, 1, FuncInfo::Pure);
}

template<typename T>
static void _reg_numeric(ClassReg& xr)
{
    xr.symbol("zero", T(0));
    xr.symbol("minval", std::numeric_limits<T>::min());
    xr.symbol("maxval", std::numeric_limits<T>::max());
}



// This functions solves the hen-and-egg problem:
// The type 'type' has itself as the underlying type. This is normally not possible to create.
void reg_type_type(RTReg& r)
{
    TDesc *desc = r.tr.mkprimDesc(PRIMTYPE_TYPE);

    // HACK: This is to init everything properly
    union
    {
        byte tmp[sizeof(DType)] = {0};
        Type ta[sizeof(DType) / sizeof(Type)];
    } u;

    // Pretend that DType::tid is set to PRIMTYPE_TYPE (yes, this is totally a valid type, trust me bro)
    enum { OffsBytes = offsetof(DType, tid), Offs = OffsBytes / sizeof(Type) };
    u.ta[Offs] = desc->h.tid;

    DType *dummy = (DType*)&u.tmp[0];
    dummy->tdesc = desc;

    DType *d = DType::GCNew(r.gc, desc, dummy);
    d->dtype = d;
    desc->h.dtype = d;

    r.types.type = d;

    ClassReg xtype = r.regclass("type", d);
}

static void reg_type_func(RTReg& r)
{
    r.tids.empty = r.tr.mklist(NULL, 0);

    DType *d = r.tr.mkprim(PRIMTYPE_FUNC);
    r.types.func = d;
    ClassReg xfunc = r.regclass("anyfunc", d);
}

static void op_uint_plus(Runtime *, Val *v) // TEMP
{
    assert(v[0].type == PRIMTYPE_UINT);
    assert(v[1].type == PRIMTYPE_UINT);
    v[0].u.ui += v[1].u.ui;
}


static void reg_type_uint(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_UINT);
    r.types.uint = d;
    ClassReg xuint = r.regclass("uint", d);
    _reg_numeric<uint>(xuint);

    xuint.op(Lexer::TOK_PLUS, op_uint_plus, PRIMTYPE_UINT, 2);
}


static void mth_int_abs(Runtime *, Val *v)
{
    assert(v->type == PRIMTYPE_SINT);
    const sint i = v->u.si;
    v->u.si = i < 0 ? -i : i; // TODO degenerate cases
}



static void reg_type_sint(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_SINT);
    r.types.sint = d;
    ClassReg xint = r.regclass("int", d);
    _reg_numeric<sint>(xint);

    {
        const Type sint1[] = { PRIMTYPE_SINT };
        const Type uint1[] = { PRIMTYPE_UINT };
        xint.method("abs", mth_int_abs, sint1, uint1);
        xint.method("iabs", mth_int_abs, sint1, sint1);
    }
}


static void mth_float_abs(Runtime *, Val *v)
{
    assert(v->type == PRIMTYPE_FLOAT);
    float f = v->u.f;
    v->u.f = f < 0 ? -f : f;
}

static void reg_type_float(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_FLOAT);
    r.types.flt = d;
    ClassReg xfloat = r.regclass("float", d);
    _reg_numeric<real>(xfloat);

    {
        const Type float1[] = { PRIMTYPE_FLOAT };
        xfloat.method("abs", mth_float_abs, float1, float1);
    }
}

/*
VMFUNC_IMM(strlen, Imm_2xu32)
{
    Val *v = LOCAL(imm->a);
    assert(s->type == PRIMTYPE_STRING);
    sref s = v->u.str;
    vm->
    NEXT();
}
*/

static void mth_string_len(Runtime *rt, Val *v)
{
    assert(v->type == PRIMTYPE_STRING);
    const sref s = v->u.str;
    v->u.str = rt->sp.lookup(s).len;
    v->type = PRIMTYPE_UINT;
}

static void reg_type_string(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_STRING);
    r.types.str = d;
    ClassReg xstr = r.regclass("string", d);

    {
        const Type str1[] = { PRIMTYPE_STRING };
        const Type uint1[] = { PRIMTYPE_UINT };
        xstr.method("len", mth_string_len, str1, uint1);
    }
}

static void reg_type_array(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_ARRAY);
    r.types.array = d;
    ClassReg xarr = r.regclass("array", d);
}

static void reg_type_table(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_TABLE);
    r.types.table = d;
    ClassReg xtab = r.regclass("table", d);
}

static void reg_type_symtab(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_SYMTAB);
    r.types.symtab = d;
    ClassReg xsymt = r.regclass("symtable", d);
}

static void reg_constants(RTReg& r)
{
    // nil is kinda special. It exists as a keyword for the parser (in particular, '-> nil' for void function returns),
    // but is also a "constant predefined identifier", in case that ever matters.
    r.symbol("nil", _Nil());

    r.symbol("true", true);
    r.symbol("false", false);
}

#include <time.h>
static void u_clock(Runtime *rt, Val *v)
{
    *v = Val((uint)clock());
}
static void u_time(Runtime *rt, Val *v)
{
    *v = Val((uint)time(NULL));
}


static void reg_test(RTReg& r)
{
    const Type uint1[] = { PRIMTYPE_UINT };
    r.regfunc(NULL, "clock", u_clock, NULL, 0, uint1, 1, FuncInfo::None);
    r.regfunc(NULL, "time", u_clock, NULL, 0, uint1, 1, FuncInfo::None);
}

void rtinit(SymTable& syms, GC& gc, StringPool& sp, TypeRegistry& tr)
{
    RTReg r = { gc, sp, tr, syms };
    reg_type_type(r);
    reg_type_func(r);
    reg_type_sint(r);
    reg_type_uint(r);
    reg_type_float(r);
    reg_type_string(r);
    reg_type_array(r);
    reg_type_table(r);
    reg_type_symtab(r);
    reg_constants(r);
    reg_test(r);
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
