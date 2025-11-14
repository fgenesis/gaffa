#pragma once

// Symbol table -- handles symbol lookup and function overloading

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

private:
    SymTable();
    Table tab;
};
