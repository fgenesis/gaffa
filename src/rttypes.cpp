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
        r.syms.addToNamespace(r.gc, cls, s, Val(obj));
    }

    template<size_t NP, size_t NR>
    inline void method(const char *name, LeafFunc lfunc, const Type (&params)[NP], const Type (&rets)[NR])
    {
        this->method(name, lfunc, params, NP, rets, NR);
    }

    void method(const char *name, LeafFunc lfunc, const Type *params, size_t nparams, const Type *rets, size_t nrets);

private:
    RTReg& r;
    Val cls;
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
    } types;

    sref str(const char *s)
    {
       return sp.put(s).id;
    }

    template<typename T>
    inline void symbol(const char *name, const T& obj)
    {
        syms.add(gc, str(name), Val(obj));
    }

    ClassReg regclass(const char *name, DType *t)
    {
        symbol(name, t);
        return ClassReg(*this, t);
    }


    void regfunc(DType *ns, const char *name, LeafFunc lfunc, const Type *params, size_t nparams, const Type *rets, size_t nrets)
    {
        Type tp = tr.mklist(params, nparams);
        Type tret = tr.mklist(rets, nrets);
        Type tf = tr.mkfunc(tp, tret);
        sref sname = str(name);

        DFunc *df = mkfunc(lfunc, tf, nparams, nrets);
        if(ns)
            syms.addToNamespace(gc, Val(ns), sname, Val(df));
        else
            syms.add(gc, sname, Val(df));
    }


    template<size_t NP, size_t NR>
    inline void regfunc(DType *ns, const char *name, LeafFunc lfunc, const Type (&params)[NP], const Type (&rets)[NR])
    {
        regfunc(ns, name, lfunc, params, NP, rets, NR);
    }

    DFunc *mkfunc(LeafFunc lfunc, Type ty, size_t nargs, size_t nrets)
    {
        DFunc *f = DFunc::GCNew(gc);
        f->info.t = ty;
        f->info.nargs = nargs;
        f->info.nrets = nrets;
        f->info.flags = FuncInfo::LFunc;
        f->info.nlocals = 0;
        f->info.nupvals = 0;
        f->u.lfunc = lfunc;
        f->dbg = NULL;
        f->dtype = types.func;
        return f;
    }
};


void ClassReg::method(const char * name, LeafFunc lfunc, const Type * params, size_t nparams, const Type * rets, size_t nrets)
{
    r.regfunc(cls.asDType(), name, lfunc, params, nparams, rets, nrets);
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
    DType *d = r.tr.mkprim(PRIMTYPE_FUNC);
    r.types.func = d;
    ClassReg xfunc = r.regclass("anyfunc", d);
}

static void reg_type_uint(RTReg& r)
{
    DType *d = r.tr.mkprim(PRIMTYPE_UINT);
    r.types.uint = d;
    ClassReg xuint = r.regclass("uint", d);
    _reg_numeric<uint>(xuint);
}


static void mth_int_abs(VM *, Val *v)
{
    assert(v->type == PRIMTYPE_SINT);
    const int i = v->u.si;
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


static void mth_float_abs(VM *, Val *v)
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


static void mth_string_len(VM *vm, Val *v)
{
    assert(v->type == PRIMTYPE_STRING);
    const sref s = v->u.str;
    v->u.str = vm->rt->sp.lookup(s).len;
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


static void reg_constants(RTReg& r)
{
    r.symbol("nil", _Nil());
    r.symbol("true", true);
    r.symbol("false", false);
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
    reg_constants(r);
}
