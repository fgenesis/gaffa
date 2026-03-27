#include "typing.h"
#include "table.h"
#include "array.h"
#include "gc.h"
#include "gaobj.h"

#include <string.h>

enum
{
     // Currently there are 1 (Array<T>) or 2 (Table<K, V>, Func<P, R>) subtypes
    MAX_SUBTYPES = 2
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
    TDesc *td = (TDesc*)gc_alloc_unmanaged_zero(gc, sz);
    if(td)
    {
        td->h.dtype = NULL;
        td->h.tid = PRIMTYPE_NIL;
        td->n = n;
        td->bits = bits;
        td->allocsize = (tsize)sz;
        td->defaultsOffset = (tsize)defaultsOffset;
        td->numdefaults = numdefaults;
        td->primtype = PRIMTYPE_NIL; // This is later replaced with the actual type
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
    td->primtype = PRIMTYPE_OBJECT;
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

Type TypeRegistry::mklist(const Type* ts, size_t n)
{
    Type dummy = PRIMTYPE_NIL;
    if(!ts)
        ts = &dummy;

    sref id = _tl.putCopy(ts, sizeof(*ts) * n);
    if(!id)
        id = 1;
    assert(id && id != -1); // FIXME: handle OOM
    return (Type)(id | TYPEBIT_TYPELIST);
}

Type TypeRegistry::lookuplist(const Type* ts, size_t n) const
{
    Type dummy = PRIMTYPE_NIL;
    if(!ts)
        ts = &dummy;

    sref id = _tl.find(ts, sizeof(*ts) * n);
    if(id)
        id |= TYPEBIT_TYPELIST;
    return (Type)id;
}

TypeIdList TypeRegistry::getlist(Type t) const
{
    assert(!t || (t & TYPEBIT_TYPELIST));
    t &= TYPEBASE_MASK;
    MemBlock mb = _tl.get(t & ~TYPEBIT_TYPELIST);
    TypeIdList tl = { (const Type*)mb.p, (tsize)mb.n / sizeof(Type) };
    return tl;
}

// FIXME: this looks like wtf...
TypeInfo TypeRegistry::getinfo(Type t) const
{
    unsigned flags = 0;
    while(t & TYPEBIT_TYPELIST)
    {
        TypeIdList tl = getlist(t);
        if(tl.n != 2)
            flags |= TYPEFLAG_TYPELIST;
        else
        {
            switch(tl.ptr[0] & TYPEBASE_MASK)
            {
                case _PRIMTYPE_X_SUBTYPE:
                    flags |= TYPEFLAG_SUBTYPE;
                    t = tl.ptr[1];
                    break;
                default:
                    flags |= TYPEFLAG_TYPELIST;
                    goto next;
            }
        }
    }
next:
    if(t < PRIMTYPE_MAX)
    {
        // Nothing to do
    }
    else if(const TDesc *desc = lookupDesc(t))
    {
        flags |= desc->bits & (TYPEFLAG_STRUCT | TYPEFLAG_UNION);
        t = desc->primtype;
    }

    TypeInfo ret;
    ret.flags = TypeFlags(flags);
    ret.base = t;
    return ret;
}

Type TypeRegistry::mksub(PrimType prim, const Type* sub, size_t n)
{
    assert(prim < PRIMTYPE_ANY);
    assert(n <= MAX_SUBTYPES);
    Type tmp[2 + MAX_SUBTYPES];
    size_t w = 0;
    tmp[w++] = _PRIMTYPE_X_SUBTYPE;
    tmp[w++] = prim;
    for(size_t i = 0; i < n; ++i)
        tmp[w++] = sub[i];

    return mklist(&tmp[0], w);
}

Type TypeRegistry::mkfunc(Type argt, Type rett)
{
    // Make sure that these are always type lists, even if it's 1 element
    if(argt != PRIMTYPE_NIL && !(argt & TYPEBIT_TYPELIST))
        argt = mklist(&argt, 1);
    if(rett != PRIMTYPE_NIL && !(rett & TYPEBIT_TYPELIST))
        rett = mklist(&rett, 1);

    Type ts[] = { argt, rett };
    return mksub(PRIMTYPE_FUNC, &ts[0], Countof(ts));
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

const TDesc *TypeRegistry::lookupDesc(Type t) const
{
    if(t & TYPEBIT_TYPELIST)
        return NULL;

    t &= TYPEBASE_MASK;

    if(t < PRIMTYPE_MAX)
        return _builtins[t];

    sref idx = t - PRIMTYPE_MAX;
    MemBlock mb = _tt.get(idx);
    return (TDesc*)mb.p;
}

DType* TypeRegistry::lookup(Type t)
{
    const TDesc *td = lookupDesc(t);
    return td ? td->h.dtype : NULL;
}

FuncDTypes TypeRegistry::lookupFunc(Type t)
{
    TypeIdList list = getlist(t);
    assert(list.n == 4 && list.ptr[0] == _PRIMTYPE_X_SUBTYPE && list.ptr[1] == PRIMTYPE_FUNC);
    FuncDTypes ret = {};
    ret.params = lookup(list.ptr[2]);
    ret.rets = lookup(list.ptr[3]);
    return ret;
}

bool TypeRegistry::isListCompatible(Type sub, Type bigger) const
{
    return sub == bigger; // FIXME
}

Type TypeRegistry::_store(TDesc *td)
{
    size_t sz = td->allocsize;
    Type ret = _tt.putTakeOver(td, sz, sz);
    assert(ret); // TODO: handle OOM
    ret += PRIMTYPE_MAX;
    return ret;
}
