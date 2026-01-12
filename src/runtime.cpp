#include "runtime.h"

Runtime::Runtime()
    : sp(gc)
    , tr(gc)
{
}

Runtime::~Runtime()
{
}

bool Runtime::init(Galloc alloc)
{
    gc.alloc = alloc;

    return sp.init() && tr.init();
}

/*
void Runtime::registerOperator(SymTable& syms, const OpRegHelper& opr)
{
    const Type tp = rt.tr.mklist(params, nparams);
    const Type tret = rt.tr.mklist(rets, nrets);
    const Type tf = rt.tr.mkfunc(tp, tret);
    const Lexer::OpName opname = Lexer::GetOperatorName(tt, nparams == 1);
    const sref sname = rt.sp.put(opname.name).id;

    DFunc * const df = DFunc::GCNew(rt.gc);
    df->info.paramtype = tp;
    df->info.rettype = tret;
    df->info.functype = tf;
    df->info.nargs = nparams;
    df->info.nrets = nrets;
    df->info.flags = FuncInfo::LFunc | FuncInfo::Pure;
    df->info.nlocals = 0;
    df->info.nupvals = 0;
    df->u.lfunc = lfunc;
    df->dbg = NULL;
    df->dtype = rt.tr.lookupDesc(PRIMTYPE_FUNC)->h.dtype;
    df->opdef = def;

    // The assumtion is that the parameter type is also the namespace
    // ie. makes sense in the case of +(uint, uint)
    syms.addToNamespace(rt.gc, params[0], sname, Val(df));
}
*/
