#include "symtable.h"
#include "gaobj.h"
#include "gc.h"


SymTable::SymTable()
    : tab(PRIMTYPE_ANY, PRIMTYPE_ANY)
{
}

SymTable::~SymTable()
{
}

void SymTable::dealloc(GC& gc)
{
    tab.dealloc(gc);
}

SymTable* SymTable::GCNew(GC& gc)
{
    void *pa = gc_new(gc, sizeof(SymTable), PRIMTYPE_SYMTAB);
    if(!pa)
        return NULL;

    return GA_PLACEMENT_NEW(pa) SymTable;
}


void SymTable::addToNamespace(GC& gc, Type ns, sref key, const Val& val)
{
    // Important: Table uses nil and xnil to denote free/unused/tombstone keys.
    // Conveniently, sref is always a valid string, and is never 0, so this can never be nil/xnil.
    static_assert(PRIMTYPE_NIL == 0, "fail");
    assert(key && ns);

    // With this property established, we can mis-use a Val key to store 2 integers instead of a regular "Val".

    ValU k;
    k.type = (PrimType)key; // dirty cast, to make the compiler shut up
    k.u.opaque = ns;

    // Lastly: Make VERY sure the GC never sees our internal table directly!
    // When trying to interpret keys as values, it's likely to crash.
    tab.set(gc, k, val);
}

const Val* SymTable::lookupInNamespace(Type ns, sref key) const
{
    assert(key && ns);
    ValU k;
    k.type = (PrimType)key;
    k.u.opaque = ns;
    return tab.getp(k);
}

const Val *SymTable::lookupObjectMethod(const GCobj* obj, sref methodname)
{
    Type t = obj->dtype->tid;
    return lookupInNamespace(t, methodname);
}


void SymTable::addSymbol(GC& gc, sref key, const Val& val)
{
    addToNamespace(gc, PRIMTYPE_NORETURN, key, val);
}

const Val* SymTable::lookupSymbol(sref key) const
{
    return lookupInNamespace(PRIMTYPE_NORETURN, key);
}
