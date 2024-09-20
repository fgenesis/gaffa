#include "typing.h"
#include "table.h"
#include "array.h"
#include "gc.h"
#include <algorithm>

static const Type NilType = {PRIMTYPE_NIL};

size_t TDesc_AllocSize(tsize numFieldsAndBits)
{
    size_t n = numFieldsAndBits & TDESC_LENMASK;
    return sizeof(TDesc) + n * sizeof(Type) + n * sizeof(sref);
}

TDesc *TDesc_New(GC& gc, tsize numFieldsAndBits, tsize extrasize)
{
    size_t sz = TDesc_AllocSize(numFieldsAndBits) + extrasize;
    TDesc *td = (TDesc*)gc_alloc_unmanaged(gc, NULL, 0, sz);
    if(td)
    {
        td->bits = numFieldsAndBits;
        td->allocsize = sz;
    }
    return td;
}

void TDesc_Delete(GC& gc, TDesc *td)
{
    size_t sz = TDesc_AllocSize(td->bits);
    gc_alloc_unmanaged(gc, td, sz, 0);
}

TypeRegistry::TypeRegistry(GC& gc)
    : _tt(gc, false)
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
    const Type table = {PRIMTYPE_TABLE};
    const Type array = {PRIMTYPE_ARRAY};
    const Type str = {PRIMTYPE_STRING};
    const Type anysub[] = { any, any };

    _builtins[PRIMTYPE_ARRAY] = mksub(PRIMTYPE_ARRAY, anysub, 1); // array = Array(any)
    _builtins[PRIMTYPE_TABLE] = mksub(PRIMTYPE_TABLE, anysub, 2); // table = Table(any, any)
    _builtins[PRIMTYPE_ERROR] = mksub(PRIMTYPE_ERROR, &str, 1);

    return true;
}

void TypeRegistry::dealloc()
{
    _tt.dealloc();
}

static bool _sortfields(const TypeAndName& a, const TypeAndName& b)
{
    return a.t.id < b.t.id;
}

Type TypeRegistry::construct(const Table& t, bool normalize)
{
    PodArray<TypeAndName> tn;
    const tsize N = t.size();
    if(!tn.resize(_tt.gc, N))
        return NilType;

    for(tsize i = 0; i < N; ++i)
    {
        const KV e = t.index(i);
        if(e.k.type.id != PRIMTYPE_STRING)
            return NilType;
        if(e.v.type.id != PRIMTYPE_TYPE) // TODO: support initializers
            return NilType;
        tn[i].name = e.k.u.str;
        tn[i].t = e.v.u.t;
    }
    if(normalize)
        std::sort(tn.data(), tn.data() + N, _sortfields);
    tsize bits = N;

    TDesc *td = TDesc_New(_tt.gc, bits, 0);
    for(size_t i = 0; i < tn.size(); ++i)
    {
        td->names()[i] = tn[i].name;
        td->types()[i] = tn[i].t;
    }
    tn.dealloc(_tt.gc);

    return _store(td);
}

Type TypeRegistry::construct(const DArray& t)
{
    const tsize N = t.size();
    TDesc *td = TDesc_New(_tt.gc, N, 0);
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
            td->types()[i] = e.u.t;
        }

    return _store(td);
}

Type TypeRegistry::mkalias(Type t)
{
    // FIXME: the counter works, but is not safe long term
    return _mkalias(t, ++_aliasCounter);
}

const TDesc *TypeRegistry::lookup(Type t) const
{
    sref idx = t.id < PRIMTYPE_MAX ? _builtins[t.id].id : t.id - PRIMTYPE_MAX;
    MemBlock mb = _tt.get(idx);
    return (TDesc*)mb.p;
}

const TDesc* TypeRegistry::getstruct(Type t) const
{
    for(;;)
    {
        const TDesc *td = lookup(t);
        if(!td)
            return NULL;
        if(!(td->bits & TDESC_BITS_IS_ALIAS))
            return td;
        t = td->t;
    }
}

Type TypeRegistry::mksub(PrimType prim, const Type* sub, size_t n)
{
    assert(prim < PRIMTYPE_ANY);

    TDesc *a = TDesc_New(_tt.gc, n, 0);
    a->t.id = prim;;
    for(size_t i = 0; i < n; ++i)
        a->types()[i] = sub[i];

    Type ret = { _tt.putCopy(&a, a->allocsize) };
    assert(ret.id); // TODO: handle OOM
    ret.id += PRIMTYPE_MAX;
    return ret;
}


Type TypeRegistry::_store(TDesc *td)
{
    size_t sz = TDesc_AllocSize(td->bits);
    Type ret = { _tt.putTakeOver(td, sz, sz) };
    assert(ret.id); // TODO: handle OOM
    ret.id += PRIMTYPE_MAX;
    return ret;
}

Type TypeRegistry::_mkalias(Type t, uint counter)
{
    if(t.id == PRIMTYPE_NIL)
        return NilType;

    // Can't make an alias to an alias type
    assert(!_isAlias(t));

    TDesc *a = TDesc_New(_tt.gc, TDESC_BITS_IS_ALIAS, sizeof(_aliasCounter));
    *((uint*)a->types()) = counter;
    a->t = t;
    Type ret = { _tt.putCopy(&a, a->allocsize) };
    assert(ret.id); // TODO: handle OOM
    ret.id += PRIMTYPE_MAX;
    return ret;
}

bool TypeRegistry::_isAlias(Type t) const
{
    const TDesc *td = lookup(t);
    return td && (td->bits & TDESC_BITS_IS_ALIAS);
}
