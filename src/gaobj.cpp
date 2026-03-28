#include "gaobj.h"
#include "gc.h"

DObj::DObj(DType * dty)
    : /*dfields(NULL),*/ nmembers(dty->numfields())
{
    this->dtype = dty;
    const _FieldDefault *def = dty->tdesc->defaults();
    const tsize n = dty->tdesc->numdefaults;
    Val * const m = memberArray();
    for(tsize i = 0; i < n; ++i)
        m[def[i].idx] = Val(def[i].u, def[i].t);
}

DObj* DObj::GCNew(GC& gc, DType* dty)
{
    const tsize nfields = dty->numfields();
    const size_t sz = sizeof(DObj) + nfields * sizeof(Val);
    void *mem = gc_new(gc, sz, PRIMTYPE_OBJECT);
    return mem ? GA_PLACEMENT_NEW(mem) DObj(dty) : NULL;
}


Val* DObj::member(const Val& key)
{
    Val idxv = dtype->fieldIndices.get(key);
    assert(idxv.type == PRIMTYPE_UINT);
    return memberArray() + idxv.u.ui;
}

tsize DObj::memberOffset(const Val* pmember) const
{
    const Val *beg = memberArray();
    assert(beg <= pmember && pmember < beg + nmembers);
    return (const char*)pmember - (const char*)this;
}

// A type is an object of the type 'type'
DType::DType(TDesc* desc, DType* typeType)
    : tid(desc->h.tid), tdesc(desc)
    , fieldIndices(PRIMTYPE_STRING, PRIMTYPE_UINT)
{
    this->dtype = typeType;
    assert(typeType->tid == PRIMTYPE_TYPE);
}

DType* DType::GCNew(GC& gc, TDesc *desc, DType* typeType)
{
    const tsize nfields = typeType->numfields();
    const size_t sz = sizeof(DType);
    void *mem = gc_new(gc, sz, PRIMTYPE_TYPE);
    return mem ? GA_PLACEMENT_NEW(mem) DType(desc, typeType) : NULL;
}

DFunc* DFunc::GCNew(GC& gc)
{
    return (DFunc*)gc_new(gc, sizeof(DFunc), PRIMTYPE_FUNC);
}

int DFunc::call(VM *vm, Val* a) const
{
    // TODO: the other function types
    assert((info.flags & FuncInfo::FuncTypeMask) == FuncInfo::LFunc);
    int r = u.lfunc(vm, a);
    assert(r < 0 || r == info.nrets);
    return r;
}


#if 0
Val *DFunc::call(VM *vm, Val* a, int *psz) const
{
    assert(*psz >= 0);

    const unsigned functype = info.flags & FuncInfo::FuncTypeMask;
    const size_t nargs = *psz;
    if(nargs < info.nargs)
    {
        assert(false && "Not enough parameters");
        return NULL;
    }
    if(nargs > info.nargs && !(info.flags & FuncInfo::VarArgs))
    {
        assert(false && "Too many parameters to non-variadic function");
        return NULL;
    }

    int nret = 0;
    if(functype == FuncInfo::LFunc)
    {
        RTError err = u.lfunc(vm, a);
        nret = err < 0 ? err : info.nrets;
        return a;
    }

    // FIXME: make vm not yieldable

    vm->pushFrame();

    const size_t req = info.nlocals + nargs;
    const VmStackAlloc sa = vm->stack_ensure(vm->cur.sp, req);
    vm->cur.sp = sa.p;
    vm->cur.sbase += sa.diff;
    Val *p = sa.p;
    if(functype == FuncInfo::GFunc)
        p += info.nrets;
    memcpy(p, a, nargs);
    if(functype == FuncInfo::GFunc)
    {
        // TODO: push frame that yields when returned to
        vm->cur.ins = u.gfunc.chunk->begin();
        nret = vm->resume();
        // TODO: *psz = actual nrets
    }
    else
    {
        nret = u.cfunc(vm, nargs, p);
    }

    Val *rets = vm->cur.sbase;

    vm->popFrame();
    vm->err = RTE_OK;

    *psz = nret;
    return nret >= 0 ? rets : NULL;
}
#endif
