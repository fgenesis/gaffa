#include "symtable.h"
#include "gaobj.h"
#include "gc.h"

static const Type _Any = { PRIMTYPE_ANY };


SimpleSymTable::SimpleSymTable()
    : regular(_Any, _Any)
{
}

SimpleSymTable::~SimpleSymTable()
{
}

void SimpleSymTable::dealloc(GC& gc)
{
    regular.dealloc(gc);
}

void SimpleSymTable::add(GC& gc, sref key, const Val& val)
{
    // Important: Table uses nil and xnil to denote free/unused/tombstone keys.
    // Conveniently, sref is always a valid string, and is never 0, so this can never be nil/xnil.
    static_assert(PRIMTYPE_NIL == 0, "fail");
    assert(key);

    // With this property established, we can mis-use a Val key to store 2 integers instead of a regular "Val".

    ValU k;
    k.type = key;

    // Basic symbols are stored under a string key and nothing else.
    // The good news is that a valid Type for ending a parameter type list is subject to deduplication,
    // yielding a non-0 ref value. So using 0 here is actually specific to name-only symbols.
    k.u.opaque = 0;


    assert(val.type < PRIMTYPE_ANY);
    if(val.type == PRIMTYPE_FUNC)
    {
        // However, functions are also registered under their arg types to allow overloading.
        k.u.opaque = val.u.func->dtype->tid;
        assert(k.u.opaque);
    }

    // Lastly: Make VERY sure the GC never sees our internal table directly!
    // When trying to interpret keys as values, it's likely to crash.
    regular.set(gc, k, val);
}

const Val* SimpleSymTable::lookupIdent(sref key) const
{
    ValU k;
    k.type = key;
    k.u.opaque = 0;
    return regular.getp(k);
}

const Val* SimpleSymTable::lookupFunc(sref key, Type argt) const
{
    assert(argt);
    ValU k;
    k.type = key;
    k.u.opaque = argt;
    const Val *p = regular.getp(k);
    if(!p)
    {
        // Function wasn't found via overloading. Maybe there's a regular (hopefully callable) symbol under that name?
        // If so use that, typechecking will make sure it's actually callable.
        k.u.opaque = 0;
        p = regular.getp(k);
    }
    return p;
}



SymTable::SymTable()
    : namespaced(_Any, _Any)
{
}

SymTable::~SymTable()
{
}

SymTable* SymTable::GCNew(GC& gc)
{
    void *pa = gc_new(gc, sizeof(SymTable), PRIMTYPE_SYMTAB);
    if(!pa)
        return NULL;

    return GA_PLACEMENT_NEW(pa) SymTable;
}

void SymTable::dealloc(GC& gc)
{
    SimpleSymTable::dealloc(gc);
    const size_t N = namespaced.values().sz;
    const Val *a = (Val*)namespaced.values().storage.p;
    for(size_t i = 0; i < N; ++i)
    {
        SimpleSymTable *p = (SimpleSymTable*)a[i].u.p;
        p->dealloc(gc);
        gc_free_unmanaged_T<SimpleSymTable>(gc, p);
    }
    namespaced.dealloc(gc);
}


void SymTable::addToNamespace(GC& gc, const Val& ns, sref key, const Val& val)
{
    Val *p = namespaced.getp(ns);
    SimpleSymTable *t;
    if(p)
        t = (SimpleSymTable*)p->u.p;
    else
    {
        t = gc_new_unmanaged_T<SimpleSymTable>(gc); // TODO: handle alloc fail
        GA_PLACEMENT_NEW(t) SimpleSymTable;
        ValU v;
        v.u.p = t;
        v.type = PRIMTYPE_OPAQUE;
        namespaced.set(gc, ns, t);
    }
    t->add(gc, key, val);
}

const Val* SymTable::lookupFuncInNamespace(const Val& ns, sref key, Type argt) const
{
    const Val *p = namespaced.getp(ns);
    if(p)
        p = ((SimpleSymTable*)p->u.p)->lookupFunc(key, argt);
    return p;
}

const Val* SymTable::lookupIdentInNamespace(const Val& ns, sref key) const
{
    const Val *p = namespaced.getp(ns);
    if(p)
        p = ((SimpleSymTable*)p->u.p)->lookupIdent(key);
    return p;
}
