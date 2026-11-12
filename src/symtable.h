#pragma once

// Symbol table -- handles symbol lookup

#include "table.h"
#include "typing.h"
#include "gaobj.h"
#include "lex.h"


class SymTable : public GCobj
{
public:

    static SymTable* SymTable::GCNew(GC& gc);

    ~SymTable();
    void dealloc(GC& gc);

    void addSymbol(GC& gc, sref key, const Val& val);
    const Val *lookupSymbol(sref key) const;

    // Any value may serve as an anchor (-> "namespace") to register symbols under a type.
    // Ie. to attach functions to types to use as methods
    void addToNamespace(GC& gc, Type ns, sref key, const Val& val);
    const Val *lookupInNamespace(Type ns, sref key) const;

    // Helpers
    const Val *lookupObjectMethod(const GCobj *obj, sref methodname);

    // Adds all entries of o (not those reachable via o.next), namespaced ones included. A key that is already
    // present with a different value becomes ambiguous: its value is replaced by a PRIMTYPE_NOTYPE marker,
    // which lookups return like any other value. The same value twice is not ambiguous.
    void importFrom(GC& gc, const SymTable& o);
    static inline bool isAmbiguous(const Val *v) { return v && v->type == PRIMTYPE_NOTYPE; }

    inline tsize size() const { return tab.size(); }

private:
    SymTable();
    Table tab; // private for a reason, this is a non-standard table and must not be exposed
public:
    // Fallback pointer to next symbol table if a symbol isn't found in this one. Careful not to form circles!
    const SymTable *next;
};
