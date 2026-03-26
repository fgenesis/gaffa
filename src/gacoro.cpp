#include "gacoro.h"
#include "runtime.h"
#include "gavm.h"


VMFUNC(_coroPrep)
{
    DCoro *co = reinterpret_cast<DCoro*>(sbase); // dirty cast
    sbase = NULL;

    VmStackAlloc sa = vm->stack_ensure(sp, 32);
    if(!sa.p)
        CHAIN(_onerror);

    sp = sa.p;
    sbase = sa.p;

    DFunc *func = (DFunc*)(co->vm.debug.errinst);
    co->vm.debug.errinst = NULL;
    sp[0] = Val(func);

    NEXT();
}

VMFUNC(_coroEnd)
{
    vm->cur.ins = NULL;
    return NULL;
}

static const struct
{
    Inst prep;
    Inst call;
    Imm_3xu32 callargs;
    Inst end;
} coroThunk =
{
    op__coroPrep,
    op_callany,
    { 0, 0, 0 },
    op__coroEnd
};

DCoro *DCoro::GCNew(Runtime *rt, DFunc *func)
{
    DCoro *co = (DCoro*)gc_new(rt->gc, sizeof(DCoro), PRIMTYPE_OPAQUE);
    if(!co)
        return NULL;

    co->vm.debug.errinst = reinterpret_cast<const Inst*>(func);
    co->vm.init(rt, &coroThunk.prep);

    return co;
}

static Val *copyargs(VM &vm, Val *a, size_t nargs)
{
    Val *dst = vm.prepareArgs(nargs);
    if(dst)
        memcpy(dst, a, nargs * sizeof(*a));
    return dst;
}

static size_t copyrets(Val *a, VM& vm, size_t maxret, size_t nrets)
{
    if(const Val *rets = vm.getReturns())
    {
        if(nrets > maxret)
            nrets = maxret;
        memcpy(a, rets, nrets * sizeof(*a));
        return nrets;
    }
    return 0;
}

int DCoro::callNoYield(Val* a, size_t nargs, size_t maxret)
{
    if(nargs && !copyargs(vm, a, nargs))
        return RTE_ALLOC_FAIL;

    for(;;)
    {
        const int res = vm.run();
        if(res >= 0)
        {
            // Yielded a result, VM must be done running
            if(vm.cur.ins)
                return RTE_NOT_YIELDABLE;

            copyrets(a, vm, maxret, res);
            return res;
        }
        if(RTIsError(res))
            return res;

        // otherwise it's a soft yield, continue running. This ignores breakpoints etc.
    }
}

int DCoro::callYield(Val* a, size_t nargs, size_t maxret)
{
    if(nargs && !copyargs(vm, a, nargs))
        return RTE_ALLOC_FAIL;

    const int res = vm.run();
    if(res >= 0)
        copyrets(a, vm, maxret, res);

    return res;
}

DCoro::Result DCoro::callEx(Val* a, size_t nargs)
{
    DCoro::Result res = {};
    if(nargs && !copyargs(vm, a, nargs))
    {
        res.status = RTE_ALLOC_FAIL;
        return res;
    }

    res.status = vm.run();
    if(res.status >= 0)
        res.rets = vm.getReturns();
    return res;
}
