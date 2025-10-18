#include "typing.h"
#include "table.h"
#include "array.h"
#include "gc.h"
#include "gaobj.h"

#include <string.h>

static const Type NilType = {PRIMTYPE_NIL};
static const Val XNil = _Xnil();


TDesc *TDesc_New(GC& gc, tsize n, u32 bits, tsize numdefaults, tsize extrasize)
{
    size_t minsz = sizeof(TDesc)
        + n * sizeof(Type)
        + n * sizeof(sref);
    size_t defaultsOffset = numdefaults
        ? alignTo(minsz, sizeof(Val::u.opaque)) // FIXME: this should be fine but find a better way to figure out alignment
        : 0;
    size_t dsz =  numdefaults * sizeof(_FieldDefault);

    size_t sz = defaultsOffset + dsz + extrasize;
    TDesc *td = (TDesc*)gc_alloc_unmanaged(gc, NULL, 0, sz);
    if(td)
    {
        td->h.dtype = NULL;
        td->h.tid.id = PRIMTYPE_NIL;
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
    : _tt(gc, false, PrefixBytes)
{
}

TypeRegistry::~TypeRegistry()
{
    _tt.dealloc();
}

bool TypeRegistry::init()
{
    memset(_builtins, 0, sizeof(_builtins));

    if(!_tt.init())
        return false;

    const Type any = {PRIMTYPE_ANY};
    const Type str = {PRIMTYPE_STRING};
    const Type anysub[] = { any, any };

    //_builtins[PRIMTYPE_ARRAY]  = mksub(PRIMTYPE_ARRAY, anysub, 1); // array = Array(any)
    //_builtins[PRIMTYPE_TABLE]  = mksub(PRIMTYPE_TABLE, anysub, 2); // table = Table(any, any)
    //_builtins[PRIMTYPE_VARARG] = mksub(PRIMTYPE_VARARG, anysub, 1); // ... = VarArg(any)
    //_builtins[PRIMTYPE_ERROR]  = mksub(PRIMTYPE_ERROR, &str, 1);

    return true;
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
        return NilType;

    tsize numdefaults = 0;
    for(tsize i = 0; i < N; ++i)
    {
        const KV e = t.index(i);
        if(e.k.type.id != PRIMTYPE_STRING)
            return NilType;
        if(e.v.type.id == PRIMTYPE_TYPE)
        {
            tn[i].defaultval = XNil;
            tn[i].t = e.v.u.t->tid;
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
    if(t.t.id == PRIMTYPE_TYPE)
        for(size_t i = 0; i < N; ++i)
        {
            td->names()[i] = 0;
            td->types()[i] = t.storage.ts[i];
        }
    else
        for(size_t i = 0; i < N; ++i)
        {
            Val e = t.dynamicLookup(i);
            if(e.type.id != PRIMTYPE_TYPE)
            {
                TDesc_Delete(_tt.gc, td);
                return NilType;
            }
            td->names()[i] = 0;
            td->types()[i] = e.u.t->tid;
        }

    return _store(td);
}

Type TypeRegistry::mklist(const sref* ts, size_t n)
{
    TDesc *td = TDesc_New(_tt.gc, n, TDESC_BITS_NO_NAMES, 0, 0);
    for(size_t i = 0; i < n; ++i)
    {
        td->names()[i] = 0;
        td->types()[i].id = ts[i];
    }
    return _store(td);
}

TDesc* TypeRegistry::mkprim(PrimType t)
{
    assert(t < Countof(_builtins));
    TDesc *td = _builtins[t];
    if(!td)
    {
        td = TDesc_New(_tt.gc, 0, 0, 0, 0);
        _builtins[t] = td;
    }
    return td;
}

Type TypeRegistry::mkfunc(Type argt, Type rett)
{
    Type ts[] = { argt, rett };
    return mksub(PRIMTYPE_FUNC, &ts[0], Countof(ts));
}

/*
Type TypeRegistry::mkfunc(const Type* arglist, size_t nargs, const Type* retlist, size_t nrets)
{
    // Both of these are just placeholders to store subtypes. Nil has no special properties so just use that.
    const Type t[] =
    {
        mksub(PRIMTYPE_NIL, arglist, nargs),
        mksub(PRIMTYPE_NIL, retlist, nrets)
    };

    return mksub(PRIMTYPE_FUNC, t, Countof(t));
}
*/

const TDesc *TypeRegistry::lookup(Type t) const
{
    if(t.id < PRIMTYPE_MAX)
        return _builtins[t.id];

    sref idx = t.id - PRIMTYPE_MAX;
    MemBlock mb = _tt.get(idx);
    return (TDesc*)mb.p;
}

Type TypeRegistry::mksub(PrimType prim, const Type* sub, size_t n)
{
    assert(prim < PRIMTYPE_ANY);

    TDesc *a = TDesc_New(_tt.gc, n, TDESC_BITS_NO_NAMES, 0, 0);
    a->primtype.id = prim;
    for(size_t i = 0; i < n; ++i)
        a->types()[i] = sub[i];

    Type ret = { _tt.putCopy(&a, a->allocsize) };
    assert(ret.id); // TODO: handle OOM
    ret.id += PRIMTYPE_MAX;
    return ret;
}


Type TypeRegistry::_store(TDesc *td)
{
    size_t sz = td->allocsize;
    Type ret = { _tt.putTakeOver(td, sz, sz) };
    assert(ret.id); // TODO: handle OOM
    ret.id += PRIMTYPE_MAX;
    return ret;
}
