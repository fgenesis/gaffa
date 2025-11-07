#include "typing.h"
#include "table.h"
#include "array.h"
#include "gc.h"
#include "gaobj.h"

#include <string.h>

enum
{
    _TLIST_BIT = 1u << (sizeof(Type) * 8 - 1)
};


static const Val XNil = _Xnil();

TDesc *TDesc_New(GC& gc, tsize n, u32 bits, tsize numdefaults, tsize extrasize)
{
    size_t minsz = sizeof(TDesc)
        + n * sizeof(Type)
        + n * sizeof(sref);

    // FIXME: this should be fine but find a better way to figure out alignment
    size_t defaultsOffset = alignTo(minsz, sizeof(Val::u.opaque));

    size_t dsz = numdefaults * sizeof(_FieldDefault);

    size_t sz = defaultsOffset + dsz + extrasize;
    TDesc *td = (TDesc*)gc_alloc_unmanaged(gc, NULL, 0, sz);
    if(td)
    {
        td->h.dtype = NULL;
        td->h.tid = PRIMTYPE_NIL;
        td->n = n;
        td->bits = bits;
        td->allocsize = (tsize)sz;
        td->defaultsOffset = (tsize)defaultsOffset;
        td->numdefaults = numdefaults;
    }
    return td;
}

void TDesc_Delete(GC& gc, TDesc *td)
{
    gc_alloc_unmanaged(gc, td, td->allocsize, 0);
}

enum
{
    PrefixBytes = sizeof(((TDesc*)NULL)->h)
};

TypeRegistry::TypeRegistry(GC& gc)
    : _tl(gc, false, 0)
    , _tt(gc, false, PrefixBytes)
{
}

TypeRegistry::~TypeRegistry()
{
    _tt.dealloc();
}

bool TypeRegistry::init()
{
    memset(_builtins, 0, sizeof(_builtins));

    return _tt.init() && _tl.init();
}

void TypeRegistry::dealloc()
{
    _tt.dealloc();
}

Type TypeRegistry::mkstruct(const StructMember* m, size_t n, size_t numdefaults)
{
    u32 bits = 0; // TODO: make union? other bits?

    TDesc *td = TDesc_New(_tt.gc, n, bits, numdefaults, 0);
    _FieldDefault *fd = td->defaults();
    for(size_t i = 0; i < n; ++i)
    {
        td->names()[i] = m[i].name;
        td->types()[i] = m[i].t;

        const Val& d = m[i].defaultval;
        if(d == XNil)
        {
            assert(numdefaults);
            --numdefaults;
            continue;
        }

        // Record fields with defaults and their index so setting them is a simple loop on object creation
        fd->idx = i;
        fd->t = d.type;
        fd->u = d.u;
        ++fd;
    }

    assert(numdefaults == 0); // Otherwise the allocation size was wrong and we're fucked now.

    return _store(td);
}

Type TypeRegistry::mkstruct(const Table& t)
{
    const tsize N = t.size();
    PodArray<StructMember> tn;
    if(!tn.resize(_tt.gc, N))
        return PRIMTYPE_NIL;

    tsize numdefaults = 0;
    for(tsize i = 0; i < N; ++i)
    {
        const KV e = t.index(i);
        if(e.k.type != PRIMTYPE_STRING)
            return PRIMTYPE_NIL;
        if(e.v.type == PRIMTYPE_TYPE)
        {
            tn[i].defaultval = XNil;
            tn[i].t = e.v.asDType()->tid;
        }
        else
        {
            tn[i].defaultval = e.v;
            tn[i].t = e.v.type;
        }
        tn[i].name = e.k.u.str;
    }

    Type ret = mkstruct(tn.data(), N, numdefaults);
    tn.dealloc(_tt.gc);

    return ret;
}

Type TypeRegistry::mkstruct(const DArray& t)
{
    const tsize N = t.size();
    TDesc *td = TDesc_New(_tt.gc, N, TDESC_BITS_NO_NAMES, 0, 0);
    if(t.t == PRIMTYPE_TYPE)
        for(size_t i = 0; i < N; ++i)
        {
            td->names()[i] = 0;
            td->types()[i] = t.storage.ts[i];
        }
    else
        for(size_t i = 0; i < N; ++i)
        {
            Val e = t.dynamicLookup(i);
            if(e.type != PRIMTYPE_TYPE)
            {
                TDesc_Delete(_tt.gc, td);
                return PRIMTYPE_NIL;
            }
            td->names()[i] = 0;
            td->types()[i] = e.asDType()->tid;
        }

    return _store(td);
}

Type TypeRegistry::mklist(const Type* ts, size_t n)
{
    /*TDesc *td = TDesc_New(_tt.gc, n, TDESC_BITS_NO_NAMES, 0, 0);
    for(size_t i = 0; i < n; ++i)
    {
        td->names()[i] = 0;
        td->types()[i] = ts[i];
    }
    return _store(td);*/


    sref id = _tl.putCopy(ts, sizeof(*ts) * n);
    if(!id)
        id = 1; // 0 means that ts was NULL. Don't want this distinction here so make it "valid" in all cases.

    return (Type)(id | _TLIST_BIT);
}

Type TypeRegistry::lookuplist(const Type* ts, size_t n) const
{
    sref id = _tl.find(ts, sizeof(*ts) * n);
    if(!id)
        id = 1; // 0 means that ts was NULL. Don't want this distinction here so make it "valid" in all cases.

    return (Type)(id | _TLIST_BIT);
}

TypeIdList TypeRegistry::getlist(Type t)
{
    assert(!t || (t & _TLIST_BIT));
    MemBlock mb = _tl.get(t & ~_TLIST_BIT);
    TypeIdList tl = { (const Type*)mb.p, (tsize)mb.n };
    return tl;
}

TDesc* TypeRegistry::mkprimDesc(PrimType t)
{
    assert(t < Countof(_builtins));
    TDesc *td = _builtins[t];
    if(!td)
    {
        td = TDesc_New(_tt.gc, 0, 0, 0, 0);
        td->primtype = t;
        td->h.tid = t;
        _builtins[t] = td;
    }
    return td;
}

DType* TypeRegistry::mkprim(PrimType t)
{
    DType *tt = lookup(PRIMTYPE_TYPE);
    assert(tt && tt->tid == PRIMTYPE_TYPE);
    TDesc *desc = mkprimDesc(t);
    assert(!desc->h.dtype);
    DType *d = DType::GCNew(_tt.gc, desc, tt);
    d->dtype = tt;
    desc->h.dtype = d;
    return d;
}

Type TypeRegistry::mkfunc(Type argt, Type rett)
{
    Type ts[] = { argt, rett };
    return mksub(PRIMTYPE_FUNC, &ts[0], Countof(ts));
}

const TDesc *TypeRegistry::lookupDesc(Type t) const
{
    if(t < PRIMTYPE_MAX)
        return _builtins[t];

    sref idx = t - PRIMTYPE_MAX;
    MemBlock mb = _tt.get(idx);
    return (TDesc*)mb.p;
}

DType* TypeRegistry::lookup(Type t)
{
    const TDesc *td = lookupDesc(t);
    assert(td);
    return td->h.dtype;
}

Type TypeRegistry::mksub(PrimType prim, const Type* sub, size_t n)
{
    assert(prim < PRIMTYPE_ANY);

    TDesc *a = TDesc_New(_tt.gc, n, TDESC_BITS_NO_NAMES, 0, 0);
    a->primtype = prim;
    for(size_t i = 0; i < n; ++i)
        a->types()[i] = sub[i];

    Type ret = _tt.putCopy(a, a->allocsize);
    assert(ret); // TODO: handle OOM
    ret += PRIMTYPE_MAX;
    return ret;
}


Type TypeRegistry::_store(TDesc *td)
{
    size_t sz = td->allocsize;
    Type ret = _tt.putTakeOver(td, sz, sz);
    assert(ret); // TODO: handle OOM
    ret += PRIMTYPE_MAX;
    return ret;
}
