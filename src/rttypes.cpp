#include "rttypes.h"
#include "typing.h"
#include "gaobj.h"
#include "symtable.h"
#include "strings.h"
#include <assert.h>


struct RTReg
{
    GC& gc;
    StringPool& sp;
    TypeRegistry& tr;
    SymTable& syms;
};


static sref strref(RTReg& r, const char *s)
{
   return r.sp.put(s).id;
}
#define S(x) strref(r, x)

DFunc *func(RTReg& r, LeafFunc lfunc, Type ty, size_t nargs, size_t nrets)
{
    DFunc *f = DFunc::GCNew(r.gc);
    f->info.t = ty;
    f->info.nargs = nargs;
    f->info.nrets = nrets;
    f->info.flags = FuncInfo::LFunc;
    f->info.nlocals = 0;
    f->info.nupvals = 0;
    f->u.lfunc = lfunc;
    f->dbg = NULL;
    f->dtype = r.tr.lookup(ty);
    assert(f->dtype); // FIXME: ??!
    return f;
}

void regfunc(RTReg& r, DType *ns, const char *name, LeafFunc lfunc, const Type *params, size_t nparams, const sref *rets, size_t nrets)
{
    Type tp = r.tr.mklist(params, nparams);
    Type tret = r.tr.mklist(rets, nrets);
    Type tf = r.tr.mkfunc(tp, tret);
    sref sname = S(name);

    DFunc *df = func(r, lfunc, tf, nparams, nrets);
    if(ns)
        r.syms.addToNamespace(r.gc, Val(ns), sname, Val(df));
    else
        r.syms.add(r.gc, sname, Val(df));
}

// This functions solves the hen-and-egg problem:
// The type 'type' has itself as the underlying type. This is normally not possible to create.
static DType *reg_type_type(RTReg& r)
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

    return d;
}

static void reg_type_uint(RTReg& r, DType *ttype)
{
    DType *d = r.tr.mkprim(PRIMTYPE_UINT);

    r.syms.add(r.gc, S("uint"), Val(d));

    r.syms.addToNamespace(r.gc, Val(d), S("zero"), Val(0u));

}


static void si_abs(VM *, Val *v)
{
    assert(v->type == PRIMTYPE_SINT);
    v->u.si = v->u.si;
}



static void reg_type_sint(RTReg& r, DType *ttype)
{
    DType *d = r.tr.mkprim(PRIMTYPE_UINT);

    r.syms.add(r.gc, S("int"), Val(d));

    r.syms.addToNamespace(r.gc, Val(d), S("zero"), Val(0));

    {
        const Type params[] = { PRIMTYPE_SINT };
        const Type rets[] = { PRIMTYPE_UINT };
        regfunc(r, d, "abs", si_abs, params, 1, rets, 1);
    }

}

void rtinit(SymTable& syms, GC& gc, StringPool& sp, TypeRegistry& tr)
{
    RTReg r = { gc, sp, tr, syms };
    DType *tt = reg_type_type(r);

    reg_type_sint(r, tt);
    reg_type_uint(r, tt);
}
