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

DFunc *func(RTReg& r, LeafFunc lfunc, Type ty)
{
    const size_t nargs = 0;
    const size_t nrets = 0;
    DFunc *f = DFunc::GCNew(r.gc);
    f->info.t = ty;
    f->info.nargs = nargs;
    f->info.nrets = nrets;
    f->info.flags = FuncInfo::LFunc;
    f->info.nlocals = 0;
    f->info.nupvals = 0;
    f->u.lfunc = lfunc;
    f->dbg = NULL;
    f->dtype = r.tr.lookup(ty)->h.dtype;
    assert(f->dtype); // FIXME: ??!
    return f;
}

void regfunc(RTReg& r, DType *ns, const char *name, LeafFunc lfunc, const StructMember *params, size_t nparams, const sref *rets, size_t nrets)
{
    Type tp = r.tr.mkstruct(params, nparams, 0);
    Type tret = r.tr.mklist(rets, nrets);
    Type tf = r.tr.mkfunc(tp, tret);
    sref sname = S(name);

    DFunc *df = func(r, lfunc, tf); 
    if(ns)
        r.syms.addToNamespace(r.gc, Val(ns), sname, Val(df));
    else
        r.syms.add(r.gc, sname, Val(df));
}

#define F(lfunc, ty) func(r, info, ty)


static void reg_type_uint(RTReg& r, DType *ttype)
{
    TDesc *desc = r.tr.mkprim(PRIMTYPE_UINT);
    DType *d = DType::GCNew(r.gc, desc, ttype);
    r.syms.add(r.gc, S("uint"), Val(d));

    r.syms.addToNamespace(r.gc, Val(d), S("zero"), Val(0u));
    
}


static void si_abs(VM *, Val *v)
{
    assert(v->type.id == PRIMTYPE_SINT);
    v->u.si = v->u.si;
}



static void reg_type_sint(RTReg& r, DType *ttype)
{
    TDesc *desc = r.tr.mkprim(PRIMTYPE_UINT);
    DType *d = DType::GCNew(r.gc, desc, ttype);
    r.syms.add(r.gc, S("int"), Val(d));

    r.syms.addToNamespace(r.gc, Val(d), S("zero"), Val(0));

    {
        const StructMember params[] = { { {PRIMTYPE_SINT}, S("x"), _Xnil() } };
        const sref rets[] = { PRIMTYPE_UINT };
        regfunc(r, d, "abs", si_abs, params, 1, rets, 1);
    }
    
}
