#pragma once

#include "gc.h"
#include "gaobj.h"

struct HLNode;
class SymTable;
struct DFunc;
class Symstore;

struct ModuleCompileResult
{
    DFunc *func;
    SymTable *exports;
};


FuncInfo prepareFuncInfo(const HLNode* node);

ModuleCompileResult compileToModule(const HLNode *node, GC& gc, const Symstore& syms, const SymTable& externals);

