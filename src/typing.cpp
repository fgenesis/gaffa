#include "typing.h"
#include "table.h"
#include "array.h"
#include "gc.h"
#include "gaobj.h"

#include <string.h>


static const Val XNil = _Xnil();

TDesc *TDesc_New(GC& gc, TypeKind kind, tsize n, tsize numdefaults, tsize extrasize)
{
    size_t minsz = sizeof(TDesc) + n * sizeof(Type);

    // Space for struct field names
    if(kind == TKIND_STRUCT)
        minsz += n * sizeof(sref);

    // Alignment for default values block
    // Given the lack of alignof() on older compilers, Val::u.opaque is the largest field
    // that controls alignment, so just use that
    STATIC_ASSERT(sizeof(Val::u.opaque) == sizeof(_FieldDefault::u));
    tsize defaultsOffset = fitIn<tsize>(alignTo(minsz, sizeof(Val::u.opaque)));
    if(!defaultsOffset)
        return NULL;

    // Size of default values block
    size_t dsz = numdefaults * sizeof(_FieldDefault);

    tsize sz = fitIn<tsize>(defaultsOffset + dsz + extrasize);
    TDesc *td = (TDesc*)gc_alloc_unmanaged_zero(gc, sz); // This handles sz == 0 -> NULL
    if(td)
    {
        td->h.dtype = NULL;
        td->h.tid = PRIMTYPE_NIL;
        td->kind = kind;
        td->objprimtype = PRIMTYPE_NIL;
        td->n = n;
        td->allocsize = sz;
        td->defaultsOffset = defaultsOffset;
        td->numdefaults = numdefaults;
    }
    return td;
}

void TDesc_Delete(GC& gc, TDesc *td)
{
    gc_alloc_unmanaged(gc, td, td->allocsize, 0);
}

// Number of bytes at the start of TDesc that don't participate in deduplication
enum
{
    PrefixBytes = sizeof(((TDesc*)NULL)->h)
};

TypeRegistry::TypeRegistry(GC& gc)
    : _dedup(gc, false, PrefixBytes)
{
}

TypeRegistry::~TypeRegistry()
{
    _dedup.dealloc();
}

bool TypeRegistry::init()
{
    memset(_builtins, 0, sizeof(_builtins));

    return _dedup.init();
}

void TypeRegistry::dealloc()
{
    _dedup.dealloc();
}

Type TypeRegistry::mkstruct(const StructMember* m, size_t n, size_t numdefaults)
{
    TDesc *td = TDesc_New(_dedup.gc, TKIND_STRUCT, n, numdefaults, 0);
    if(!td)
        return PRIMTYPE_NIL;
    td->objprimtype = PRIMTYPE_OBJECT;
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
    Type ret = PRIMTYPE_NIL;
    const tsize N = t.size();
    PodArray<StructMember> tn;
    if(!tn.resize(_dedup.gc, N))
        return PRIMTYPE_NIL;

    tsize numdefaults = 0;
    for(tsize i = 0; i < N; ++i)
    {
        const KV e = t.index(i);
        if(e.k.type != PRIMTYPE_STRING)
            goto fail;
        if(e.v.type == PRIMTYPE_TYPE)
        {
            tn[i].defaultval = XNil;
            const DType *dt = e.v.asDType();
            if(!canDeclareVar(dt->tdesc->kind))
                goto fail;
            tn[i].t = dt->tid;
        }
        else
        {
            tn[i].defaultval = e.v;
            tn[i].t = e.v.type;
        }
        tn[i].name = e.k.u.str;
    }

    ret = mkstruct(tn.data(), N, numdefaults);

fail:
    tn.dealloc(_dedup.gc);
    return ret;
}

Type TypeRegistry::mklist(const Type* ts, size_t n)
{
    TDesc *td = _listConcatFlatten(TKIND_TYPELIST, ts, n);
    return _store(td);
}

Type TypeRegistry::mksub(PrimType prim, const Type* sub, size_t n)
{
    assert(prim < PRIMTYPE_ANY);
    assert(n <= MAX_SUBTYPES);
    Type tmp[1 + MAX_SUBTYPES] = { (Type)prim };
    for(size_t i = 0; i < n; ++i)
        tmp[i+1] = sub[i];

    TDesc *td = _mklist(TKIND_SUBTYPED, &tmp[0], n+1);
    td->objprimtype = prim;
    return _store(td);
}

Type TypeRegistry::mkfunc(Type argt, Type rett)
{
    Type tmp[] = { PRIMTYPE_FUNC, argt, rett };
    TDesc *td = _mklist(TKIND_SUBTYPED, &tmp[0], Countof(tmp));
    td->objprimtype = PRIMTYPE_FUNC;
    return _store(td);
}

TDesc* TypeRegistry::mkprimDesc(PrimType t)
{
    assert(t < Countof(_builtins));
    TDesc *td = _builtins[t];
    if(!td)
    {
        td = TDesc_New(_dedup.gc, TKIND_PRIMTYPE, 0, 0, 0);
        td->objprimtype = t;
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
    DType *d = DType::GCNew(_dedup.gc, desc, tt);
    d->dtype = tt;
    desc->h.dtype = d;
    return d;
}

const TDesc *TypeRegistry::lookupDesc(Type t) const
{
    t &= TYPEBASE_MASK;

    if(t < PRIMTYPE_MAX)
        return _builtins[t];

    sref idx = t - PRIMTYPE_MAX;
    MemBlock mb = _dedup.get(idx);
    return (TDesc*)mb.p;
}

DType *TypeRegistry::lookup(Type t)
{
    const TDesc *td = lookupDesc(t);
    return td ? td->h.dtype : NULL;
}

const TDesc *TypeRegistry::lookupFunc(FuncTypes *ft, Type t)
{
    const TDesc *td = lookupDesc(t);
    if(td->kind != TKIND_SUBTYPED)
        return NULL;
    const Type *ts = td->types();
    if(td->n != 3 || ts[0] != PRIMTYPE_FUNC)
        return NULL;

    if(ft)
    {
        ft->params = _aslist(ts[1]);
        ft->rets = _aslist(ts[2]);
    }
    return td;
}

bool TypeRegistry::isListCompatible(Type sub, Type bigger) const
{
    return sub == bigger; // FIXME
}

// This is mainly for figuring out the size before concatenating typelists.
size_t TypeRegistry::_listLenRec(const Type *ts, size_t n)
{
    size_t ret = 0;
    for(size_t i = 0; i < n; ++i)
    {
        Type t = ts[i];
        if(t < PRIMTYPE_MAX)
        {
            ret += 1;
            continue;
        }

        const TDesc *d = lookupDesc(t);
        if(d->kind != TKIND_TYPELIST)
        {
            ret += 1;
            continue;
        }

        ret += _listLenRec(d->types(), d->n);
    }
    return ret;
}

TDesc* TypeRegistry::_mklist(TypeKind kind, const Type* ts, size_t n)
{
    TDesc *d = TDesc_New(_dedup.gc, kind, n, 0, 0);
    memcpy(d->types(), ts, n * sizeof(*ts));
    return d;
}

bool TypeRegistry::_checkMustBeObjTypes(const Type* ts, size_t n)
{
    for(size_t i = 0; i < n; ++i)
    {
        Type t = ts[i];
        if(t < PRIMTYPE_MAX)
            continue;

        const TDesc *d = lookupDesc(t);
        if(!isObjectKind(d->kind))
            return false;
    }

    return true;
}

TypeIdList TypeRegistry::_aslist(Type t)
{
    const TDesc *td = lookupDesc(t);
    TypeIdList ret;
    if(td->kind == TKIND_TYPELIST)
    {
        ret.ptr = td->types();
        ret.n = td->n;
    }
    else
    {
        ret.ptr = &td->h.tid; // This doesn't move in memory
        ret.n = 1;
    }
    return ret;
}

// Typelist concatenation results in a new typelist. Given these:
//   A = (int, float)
//   B = (string, string)
//   T = Table(string, any)  <-- note that this is internally a typelist, too, but not semantically
//   TL = typelist(A, Array(int), bool, B, T)
// Then TL is:
//   ((int, float), Array(int), bool, (string, string), Table(string, any))
// Which is flattened to:
//   (int, float, Array(int), bool, string, string, Table(string, any))
// Because plain typelists can't be nested.
// Important: ONLY plain typelists are flattened! All other type kinds stay
// as they are, even if they are internally implemented as a list of types.
TDesc* TypeRegistry::_listConcatFlatten(TypeKind kind, const Type* ts, size_t n)
{
    size_t len = _listLenRec(ts, n);
    TDesc *d = TDesc_New(_dedup.gc, kind, len, 0, 0);
    Type *end = _listCopy(d->types(), ts, n);
    assert(d->types() + len == end);
    return d;
}

Type* TypeRegistry::_listCopy(Type* dst, const Type* ts, size_t n)
{
    for(size_t i = 0; i < n; ++i)
    {
        Type t = ts[i];
        if(t < PRIMTYPE_MAX)
        {
            *dst++ = t;
            continue;
        }

        const TDesc *d = lookupDesc(t);
        if(d->kind != TKIND_TYPELIST)
        {
            *dst++ = t;
            continue;
        }

        dst = _listCopy(dst, d->types(), d->n); // Flatten it
    }
    return dst;
}

Type TypeRegistry::_store(TDesc *td)
{
    size_t sz = td->allocsize;
    sref id = _dedup.putTakeOver(td, sz, sz);
    assert(id); // TODO: handle OOM

    // Record the actually used type id
    td = (TDesc*)_dedup.get(id).p;
    Type ret = (Type)(id + PRIMTYPE_MAX);
    td->h.tid = ret;
    return ret;
}
