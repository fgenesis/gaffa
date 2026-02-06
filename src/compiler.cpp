#include "compiler.h"
#include "gc.h"
#include "hlir.h"
#include "gaobj.h"
#include "symtable.h"


FuncInfo prepareFuncInfo(const HLNode* node)
{
    assert(node->type == HLNODE_FUNCTION);
    FuncInfo info = {0};

    const HLFunction *hlfunc = node->as<HLFunction>();
    const HLFunctionHdr *fhdr = hlfunc->hdr->as<HLFunctionHdr>();
    const HLList *body = hlfunc->body->as<HLList>();

    int nargs = fhdr->nargs();
    int nrets = fhdr->nrets();

    u32 fflags = FuncInfo::GFunc;
    if(nargs < 0)
        fflags |= FuncInfo::VarArgs;
    if(nrets < 0)
        fflags |= FuncInfo::VarRets;

    info.flags = fflags;
    info.nargs = nargs < 0 ? -nargs+1 : nargs;
    info.nrets = nrets < 0 ? -nrets+1 : nrets;

    // These are filled later
    info.fixedstack = 0;
    info.nupvals = 0;

    return info;
}

ModuleCompileResult compileToModule(HLNode* node, GC& gc, const SymTable& symtab)
{
    return ModuleCompileResult();
}
