#pragma once

// Symbol table -- handles symbol lookup and function overloading

#include "table.h"
#include "typing.h"
#include "gaobj.h"

class SimpleSymTable
{
public:
    SimpleSymTable();
    ~SimpleSymTable();
    void dealloc(GC& gc);

    // Regular symbol lookup -- with optional function overloading
    void add(GC& gc, sref key, const Val& val);
    const Val *lookupIdent(sref key) const;
    // When looking up functions, pass the arg list type so we can figure out which overload to select
    const Val *lookupFunc(sref key, Type argt) const;

protected:
    Table regular;
};


class SymTable : public GCobj, public SimpleSymTable
{
public:

    static SymTable* SymTable::GCNew(GC& gc);

    ~SymTable();
    void dealloc(GC& gc);

    // Any value may serve as an anchor (-> "namespace") to register symbols under
    // Typically that would be type values, ie. to attach functions to types to use as methods
    void addToNamespace(GC& gc, const Val& ns, sref key, const Val& val);
    const Val *lookupFuncInNamespace(const Val& ns, sref key, Type argt) const;
    const Val *lookupIdentInNamespace(const Val& ns, sref key) const;

private:
    SymTable();
    Table namespaced;
};
